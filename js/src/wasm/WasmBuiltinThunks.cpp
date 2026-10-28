/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "wasm/WasmBuiltinThunks.h"

#include "mozilla/Atomics.h"
#include "mozilla/EnumeratedArray.h"
#include "mozilla/EnumeratedRange.h"
#include "mozilla/UniquePtr.h"

#include "jit/ExecutableAllocator.h"
#include "jit/MacroAssembler.h"
#include "threading/LockGuard.h"
#include "threading/Mutex.h"
#include "vm/Stack.h"
#include "wasm/WasmFrameIterator.h"
#include "wasm/WasmTypes.h"

#include "jit/MacroAssembler-inl.h"

using namespace js;
using namespace js::jit;
using namespace js::wasm;

namespace {

#if defined(JS_CODEGEN_X86)

struct ThunkRange {
    uint32_t begin;
    uint32_t pushedFP;
    uint32_t setFP;
    uint32_t poppedFP;
    uint32_t ret;
    uint32_t end;
};

struct BuiltinThunks {
    uint8_t* code = nullptr;
    size_t length = 0;
    Vector<ThunkRange, 0, SystemAllocPolicy> ranges;
    mozilla::EnumeratedArray<SymbolicAddress, SymbolicAddress::Limit, uint32_t> offsets;

    ~BuiltinThunks() {
        if (code)
            DeallocateExecutableMemory(code, length);
    }
};

// Initialization can allocate executable memory (whose mutex order is 500).
// It runs outside the Module's publication lock and makes no embedding calls.
static Mutex initMutex(MutexId { "WasmBuiltinThunks", 450 });
static mozilla::Atomic<BuiltinThunks*, mozilla::ReleaseAcquire> builtinThunks(nullptr);

static bool
NeedsThunk(SymbolicAddress address)
{
    switch (CanonicalSymbolicAddress(address)) {
      case SymbolicAddress::LookupFrameTLS:
      case SymbolicAddress::HandleThrow:
      case SymbolicAddress::HandleExecutionInterrupt:
      case SymbolicAddress::ReportTrap:
      case SymbolicAddress::ReportOutOfBounds:
      case SymbolicAddress::ReportUnalignedAccess:
      case SymbolicAddress::CallImport_Void:
      case SymbolicAddress::CallImport_I32:
      case SymbolicAddress::CallImport_I64:
      case SymbolicAddress::CallImport_F64:
      case SymbolicAddress::CoerceInPlace_ToInt32:
      case SymbolicAddress::CoerceInPlace_ToNumber:
        // These are called by stubs which already publish exit/interrupt state.
        return false;
      default:
        MOZ_ASSERT(address != SymbolicAddress::Limit);
        return true;
    }
}

static uint32_t
NativeArgumentBytes(ABIFunctionType type)
{
    uint32_t arguments = uint32_t(type) >> ArgType_Shift;
    uint32_t bytes = 0;
    while (arguments) {
        switch (arguments & ArgType_Mask) {
          case ArgType_General:
          case ArgType_Float32:
            bytes += 4;
            break;
          case ArgType_Double:
          case ArgType_Int64:
            bytes += 8;
            break;
          default:
            MOZ_CRASH("Unsupported native Wasm argument");
        }
        arguments >>= ArgType_Shift;
    }
    return bytes;
}

static void
GenerateThunk(MacroAssembler& masm, SymbolicAddress address, ThunkRange* range)
{
    masm.haltingAlign(CodeAlignment);
    masm.setFramePushed(0);
    range->begin = masm.currentOffset();

    // Explicit-instance callers supply ESI through a fixed compiler operand.
    // Unannotated system-ABI callers may use ESI for an arbitrary live value;
    // recover their TLS from the nearest instance-switching caller instead.
    // Native arguments remain above the two-word header, not above a
    // FrameWithInstances. The helper neither allocates nor enters JS/GC.
    masm.push(FramePointer);
    range->pushedFP = masm.currentOffset();
    masm.moveStackPtrTo(FramePointer);
    range->setFP = masm.currentOffset();

    Register scratch = ecx;
    static const int32_t SavedESI = -int32_t(sizeof(void*));
    static const int32_t SavedTLS = -2 * int32_t(sizeof(void*));
    static const uint32_t LocalBytes = 2 * sizeof(void*);
    static_assert(sizeof(Frame) + LocalBytes == ABIStackAlignment,
                  "Native thunk locals preserve x86 call alignment");
    masm.reserveStack(LocalBytes);
    masm.storePtr(WasmTlsReg, Address(FramePointer, SavedESI));
    if (!SymbolicAddressRequiresTLS(address)) {
        masm.reserveStack(ABIStackAlignment);
        masm.loadPtr(Address(FramePointer, offsetof(Frame, callerFP)), scratch);
        masm.storePtr(scratch, Address(masm.getStackPointer(), 0));
        masm.call(ImmPtr(JS_FUNC_TO_DATA_PTR(void*, LookupFrameTLS)));
        masm.freeStack(ABIStackAlignment);
        masm.movePtr(ReturnReg, WasmTlsReg);
    }
    masm.storePtr(WasmTlsReg, Address(FramePointer, SavedTLS));
    masm.loadPtr(Address(WasmTlsReg, offsetof(TlsData, cx)), scratch);
    masm.loadPtr(Address(scratch, JSContext::offsetOfWasmActivation()), scratch);
    masm.store32(Imm32(uint32_t(ExitReason::Native)),
                 Address(scratch, WasmActivation::offsetOfExitReason()));
    masm.storePtr(FramePointer, Address(scratch, WasmActivation::offsetOfFP()));

    uint32_t bytes = NativeArgumentBytes(SymbolicAddressSignature(address));
    // All x86 callers align the outgoing native argument area to 16 bytes.
    // Our two-word header plus two locals preserve that alignment here.
    uint32_t framePushed = AlignBytes(bytes, ABIStackAlignment);
    masm.reserveStack(framePushed);
    for (uint32_t offset = 0; offset < bytes; offset += sizeof(uint32_t)) {
        masm.load32(Address(FramePointer, sizeof(Frame) + offset), scratch);
        masm.store32(scratch, Address(masm.getStackPointer(), offset));
    }
    masm.call(ImmPtr(AddressOf(address)));

    // Do not consume the x87 result or clobber EAX:EDX. Existing Baseline,
    // Ion and MacroAssembler call sites still perform their one conversion.
    masm.loadPtr(Address(FramePointer, SavedTLS), scratch);
    masm.loadPtr(Address(scratch, offsetof(TlsData, cx)), scratch);
    masm.loadPtr(Address(scratch, JSContext::offsetOfWasmActivation()), scratch);
    masm.storePtr(ImmWord(0), Address(scratch, WasmActivation::offsetOfFP()));
    masm.store32(Imm32(uint32_t(ExitReason::None)),
                 Address(scratch, WasmActivation::offsetOfExitReason()));
    masm.freeStack(framePushed);
    masm.loadPtr(Address(FramePointer, SavedESI), WasmTlsReg);
    masm.freeStack(LocalBytes);
    masm.pop(FramePointer);
    range->poppedFP = masm.currentOffset();
    range->ret = masm.currentOffset();
    masm.ret();
    range->end = masm.currentOffset();
}

static const ThunkRange*
FindRange(const BuiltinThunks& thunks, void* pc)
{
    uintptr_t value = uintptr_t(pc);
    if (value < uintptr_t(thunks.code) || value >= uintptr_t(thunks.code) + thunks.length)
        return nullptr;
    uint32_t offset = uint32_t(value - uintptr_t(thunks.code));
    // There are only a few dozen process-wide immutable thunks.
    for (const ThunkRange& range : thunks.ranges) {
        if (offset >= range.begin && offset < range.end)
            return &range;
    }
    return nullptr;
}

#endif // JS_CODEGEN_X86

} // anonymous namespace

bool
wasm::EnsureBuiltinThunksInitialized()
{
#if defined(JS_CODEGEN_X86)
    if (builtinThunks)
        return true;
    LockGuard<Mutex> lock(initMutex);
    if (builtinThunks)
        return true;
    auto thunks = mozilla::MakeUnique<BuiltinThunks>();
    if (!thunks)
        return false;
    LifoAlloc lifo(64 * 1024);
    TempAllocator allocator(&lifo);
    MacroAssembler masm(MacroAssembler::WasmToken(), allocator);
    for (auto address : mozilla::MakeEnumeratedRange(SymbolicAddress::Limit)) {
        thunks->offsets[address] = UINT32_MAX;
        if (!NeedsThunk(address))
            continue;
        ThunkRange range;
        GenerateThunk(masm, address, &range);
        if (masm.oom() || !thunks->ranges.append(range))
            return false;
        thunks->offsets[address] = range.begin;
    }
    masm.finish();
    if (masm.oom())
        return false;
    thunks->length = AlignBytes(masm.bytesNeeded(), ExecutableCodePageSize);
    thunks->code = static_cast<uint8_t*>(
        AllocateExecutableMemory(thunks->length, ProtectionSetting::Writable));
    if (!thunks->code)
        return false;
    {
        AutoFlushICache flush("WasmBuiltinThunks");
        AutoFlushICache::setRange(uintptr_t(thunks->code), thunks->length);
        masm.executableCopy(thunks->code);
        masm.processCodeLabels(thunks->code);
        memset(thunks->code + masm.bytesNeeded(), 0, thunks->length - masm.bytesNeeded());
    }
    if (!ExecutableAllocator::makeExecutable(thunks->code, thunks->length))
        return false;
    builtinThunks = thunks.release();
#endif
    return true;
}

void
wasm::ReleaseBuiltinThunks()
{
#if defined(JS_CODEGEN_X86)
    // Engine shutdown joins helper threads and requires all runtimes gone.
    BuiltinThunks* thunks = builtinThunks;
    builtinThunks = nullptr;
    js_delete(thunks);
#endif
}

void*
wasm::SymbolicAddressTarget(SymbolicAddress address)
{
#if defined(JS_CODEGEN_X86)
    if (NeedsThunk(address)) {
        BuiltinThunks* thunks = builtinThunks;
        MOZ_RELEASE_ASSERT(thunks);
        MOZ_RELEASE_ASSERT(thunks->offsets[address] != UINT32_MAX);
        return thunks->code + thunks->offsets[address];
    }
#endif
    return AddressOf(address);
}

bool
wasm::LookupBuiltinThunk(void* pc)
{
#if defined(JS_CODEGEN_X86)
    BuiltinThunks* thunks = builtinThunks;
    return thunks && FindRange(*thunks, pc);
#else
    return false;
#endif
}

bool
wasm::UnwindBuiltinThunk(void* pc, void* sp, void* physicalFP,
                         void** callerPC, uint8_t** callerFP, void** stackAddress)
{
#if defined(JS_CODEGEN_X86)
    BuiltinThunks* thunks = builtinThunks;
    const ThunkRange* range = thunks ? FindRange(*thunks, pc) : nullptr;
    if (!range || !sp || !physicalFP)
        return false;
    uint32_t offset = uint32_t(uintptr_t(pc) - uintptr_t(thunks->code));
    void** stack = static_cast<void**>(sp);
    *callerFP = static_cast<uint8_t*>(physicalFP);
    *stackAddress = sp;
    if (offset < range->pushedFP)
        *callerPC = stack[0];
    else if (offset < range->setFP)
        *callerPC = stack[1];
    else if (offset < range->poppedFP) {
        Frame* frame = static_cast<Frame*>(physicalFP);
        *callerPC = frame->returnAddress;
        *callerFP = frame->callerFP;
        *stackAddress = frame;
    } else
        *callerPC = stack[0];
    return *callerPC && *callerFP;
#else
    return false;
#endif
}

size_t
wasm::BuiltinThunksCodeSize()
{
#if defined(JS_CODEGEN_X86)
    BuiltinThunks* thunks = builtinThunks;
    return thunks ? thunks->length : 0;
#else
    return 0;
#endif
}
