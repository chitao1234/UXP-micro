/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * Copyright 2014 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "wasm/WasmFrameIterator.h"

#include "wasm/WasmInstance.h"
#include "wasm/WasmBuiltinThunks.h"

#include "jit/MacroAssembler-inl.h"

using namespace js;
using namespace js::jit;
using namespace js::wasm;

using mozilla::DebugOnly;
using mozilla::Swap;

/*****************************************************************************/
// FrameIterator implementation

static void*
ReturnAddressFromFP(void* fp)
{
    return reinterpret_cast<Frame*>(fp)->returnAddress;
}

static uint8_t*
CallerFPFromFP(void* fp)
{
    return reinterpret_cast<Frame*>(fp)->callerFP;
}

FrameIterator::FrameIterator()
  : activation_(nullptr),
    code_(nullptr),
    callsite_(nullptr),
    codeRange_(nullptr),
    fp_(nullptr),
    pc_(nullptr),
    missingFrameMessage_(false)
{
    MOZ_ASSERT(done());
}

FrameIterator::FrameIterator(const WasmActivation& activation)
  : activation_(&activation), code_(nullptr), callsite_(nullptr), codeRange_(nullptr),
    fp_(nullptr), pc_(nullptr), missingFrameMessage_(false)
{
    if (void* resumePC = activation.resumePC()) {
#if defined(JS_CODEGEN_X86)
        pc_ = static_cast<uint8_t*>(activation.interruptUnwindPC());
        fp_ = activation.interruptFP();
        if (!fp_ || !pc_)
            return;
        code_ = activation.compartment()->wasm.lookupCode(pc_);
        if (!code_)
            return;
        codeRange_ = code_->lookupRange(pc_);
        if (!codeRange_ || !codeRange_->isFunction())
            codeRange_ = nullptr;
#else
        pc_ = static_cast<uint8_t*>(resumePC);
        code_ = activation.compartment()->wasm.lookupCode(resumePC);
        MOZ_ASSERT(code_);
        codeRange_ = code_->lookupRange(resumePC);
#endif
        return;
    }
    fp_ = activation.fp();
    if (fp_)
        settle();
}

bool
FrameIterator::done() const
{
    return !codeRange_;
}

void
FrameIterator::operator++()
{
    MOZ_ASSERT(!done());
    if (fp_)
        settle();
    else
        codeRange_ = nullptr;
}

void
FrameIterator::settle()
{
    void* returnAddress = ReturnAddressFromFP(fp_);
    fp_ = CallerFPFromFP(fp_);
    if (!fp_) {
        pc_ = nullptr;
        code_ = nullptr;
        codeRange_ = nullptr;
        callsite_ = nullptr;
        return;
    }
    code_ = activation_->compartment()->wasm.lookupCode(returnAddress);
    MOZ_ASSERT(code_);
    codeRange_ = code_->lookupRange(returnAddress);
    MOZ_ASSERT(codeRange_ && codeRange_->kind() == CodeRange::Function);
    callsite_ = code_->lookupCallSite(returnAddress);
    // Native builtin callouts historically do not all record CallSites.
    // Their real exit frame still identifies the owning function; report its
    // starting bytecode location when no finer callsite is available.
    pc_ = static_cast<uint8_t*>(returnAddress);
}

const char*
FrameIterator::filename() const
{
    MOZ_ASSERT(!done());
    return code_->metadata().filename.get();
}

const char16_t*
FrameIterator::displayURL() const
{
    MOZ_ASSERT(!done());
    return code_->metadata().displayURL();
}

bool
FrameIterator::mutedErrors() const
{
    MOZ_ASSERT(!done());
    return code_->metadata().mutedErrors();
}

JSAtom*
FrameIterator::functionDisplayAtom() const
{
    MOZ_ASSERT(!done());

    JSContext* cx = activation_->cx();

    if (missingFrameMessage_) {
        const char* msg = "asm.js/wasm frames may be missing below this one";
        JSAtom* atom = Atomize(cx, msg, strlen(msg));
        if (!atom) {
            cx->clearPendingException();
            return cx->names().empty;
        }

        return atom;
    }

    MOZ_ASSERT(codeRange_);

    JSAtom* atom = code_->getFuncAtom(cx, codeRange_->funcIndex());
    if (!atom) {
        cx->clearPendingException();
        return cx->names().empty;
    }

    return atom;
}

unsigned
FrameIterator::lineOrBytecode() const
{
    MOZ_ASSERT(!done());
    return callsite_ ? callsite_->lineOrBytecode()
                     : (codeRange_ ? codeRange_->funcLineOrBytecode() : 0);
}

/*****************************************************************************/
// Prologue/epilogue code generation

// These constants reflect statically-determined offsets in the
// prologue/epilogue. The offsets are dynamically asserted during code
// generation.
#if defined(JS_CODEGEN_X64)
static const unsigned PushedRetAddr = 0;
static const unsigned PushedPadding = 4;
static const unsigned PushedTLS = PushedPadding + 2;
// r14->rax and a normal [rax+disp] load; the offsets differ across builds.
static const unsigned PushedFP = PushedTLS + 3 +
    (JSContext::offsetOfWasmActivation() < 128 ? 4 : 7) +
    (WasmActivation::offsetOfFP() < 128 ? 3 : 6);
static const unsigned StoredFP = PushedFP +
    (WasmActivation::offsetOfFP() < 128 ? 4 : 7);
static const unsigned PostStorePrePopFP = 4;
static const unsigned ReturnBytes = 1;
#elif defined(JS_CODEGEN_X86)
static const unsigned PushedRetAddr = 0;
static const unsigned PushedPadding = 0;
static const unsigned PushedTLS = 0;
static const unsigned PushedFP = 1;
static const unsigned StoredFP = 3;
static const unsigned PoppedFPToRet = 0;
static const unsigned PostStorePrePopFP = PoppedFPToRet;
static const unsigned ReturnBytes = 1;
#elif defined(JS_CODEGEN_ARM)
static const unsigned PushedRetAddr = 4;
static const unsigned PushedPadding = 8;
static const unsigned PushedTLS = 12;
static const unsigned PushedFP = 28;
static const unsigned StoredFP = 32;
static const unsigned PostStorePrePopFP = 4;
static const unsigned ReturnBytes = 4;
#elif defined(JS_CODEGEN_MIPS32) || defined(JS_CODEGEN_MIPS64) || \
      defined(JS_CODEGEN_LOONGARCH64)
// These targets reserve the entire Frame before writing its fields.
static const unsigned PushedRetAddr = 8;
static const unsigned PushedPadding = 8;
static const unsigned PushedTLS = 12;
static const unsigned PushedFP = 28;
static const unsigned StoredFP = 32;
static const unsigned PostStorePrePopFP = 4;
# if defined(JS_CODEGEN_LOONGARCH64)
static const unsigned ReturnBytes = 12;
# else
static const unsigned ReturnBytes = 16;
# endif
#elif defined(JS_CODEGEN_ARM64) || defined(JS_CODEGEN_NONE)
// ARM64 wasm entry/interrupt stubs are already unimplemented in this tree.
static const unsigned PushedRetAddr = 0;
static const unsigned PushedPadding = 0;
static const unsigned PushedTLS = 0;
static const unsigned PushedFP = 0;
static const unsigned StoredFP = 0;
static const unsigned PostStorePrePopFP = 0;
static const unsigned ReturnBytes = 0;
#else
# error "Unknown architecture!"
#endif

// Generate a fixed header that keeps the exact callee TLS recoverable without
// reserving WasmTlsReg throughout Baseline or Ion function bodies.
static void
GenerateCallablePrologue(MacroAssembler& masm, unsigned framePushed, ExitReason reason,
                         uint32_t* entry)
{
#if defined(JS_CODEGEN_X86)
    *entry = masm.currentOffset();
    masm.push(FramePointer);
    MOZ_ASSERT_IF(!masm.oom(), PushedFP == masm.currentOffset() - *entry);
    masm.moveStackPtrTo(FramePointer);
    MOZ_ASSERT_IF(!masm.oom(), StoredFP == masm.currentOffset() - *entry);
    if (reason == ExitReason::Trap) {
        // Ordinary function bodies may repurpose ESI. A bad-signature far
        // jump can also arrive before its callee has made a frame. In both
        // cases our saved caller FP identifies the frame owning the trap.
        // Recover once per shared trap exit, before publishing an exit or
        // entering any code that can allocate, report an error or run JS.
        uint32_t callBytes = StackDecrementForCall(ABIStackAlignment,
                                                  sizeof(Frame), sizeof(void*));
        masm.reserveStack(callBytes);
        Register scratch = ABINonArgReg0;
        masm.loadPtr(Address(FramePointer, offsetof(Frame, callerFP)), scratch);
        masm.storePtr(scratch, Address(masm.getStackPointer(), 0));
        masm.call(SymbolicAddress::LookupFrameTLS);
        masm.freeStack(callBytes);
        masm.movePtr(ReturnReg, WasmTlsReg);
    }
    if (reason != ExitReason::None) {
        // Exit stubs can be entered by a trap as well as an instance switch.
        // Record their stable owner before publishing the physical exit frame.
        masm.storePtr(WasmTlsReg, Address(FramePointer, FrameCalleeTLSOffset));
        Register scratch = ABINonArgReg0;
        masm.loadWasmActivationFromTls(scratch);
        masm.store32(Imm32(int32_t(reason)), Address(scratch, WasmActivation::offsetOfExitReason()));
        masm.storePtr(FramePointer, Address(scratch, WasmActivation::offsetOfFP()));
    }
    if (framePushed)
        masm.subFromStackPtr(Imm32(framePushed));
#else
    Register scratch = ABINonArgReg0;
    {
#if defined(JS_CODEGEN_ARM)
        AutoForbidPools afp(&masm, /* number of instructions in scope = */ 8);
#endif
        *entry = masm.currentOffset();

#if defined(JS_CODEGEN_MIPS32) || defined(JS_CODEGEN_MIPS64) || \
    defined(JS_CODEGEN_LOONGARCH64)
        masm.subFromStackPtr(Imm32(sizeof(Frame)));
        masm.storePtr(ra, Address(masm.getStackPointer(), offsetof(Frame, returnAddress)));
        MOZ_ASSERT_IF(!masm.oom(), PushedRetAddr == masm.currentOffset() - *entry);
        masm.storePtr(WasmTlsReg, Address(masm.getStackPointer(), offsetof(Frame, tls)));
        MOZ_ASSERT_IF(!masm.oom(), PushedTLS == masm.currentOffset() - *entry);
        masm.loadWasmActivationFromTls(scratch);
        Register scratch2 = ABINonArgReg1;
        masm.loadPtr(Address(scratch, WasmActivation::offsetOfFP()), scratch2);
        masm.storePtr(scratch2, Address(masm.getStackPointer(), offsetof(Frame, callerFP)));
#elif defined(JS_CODEGEN_ARM64) || defined(JS_CODEGEN_NONE)
        MOZ_CRASH("Wasm callable prologue is not implemented on this backend");
#else
# if defined(JS_CODEGEN_ARM)
        masm.push(lr);
# endif
        MOZ_ASSERT_IF(!masm.oom(), PushedRetAddr == masm.currentOffset() - *entry);
        masm.subFromStackPtr(Imm32(sizeof(void*)));
        MOZ_ASSERT_IF(!masm.oom(), PushedPadding == masm.currentOffset() - *entry);
        masm.push(WasmTlsReg);
        MOZ_ASSERT_IF(!masm.oom(), PushedTLS == masm.currentOffset() - *entry);
        masm.loadWasmActivationFromTls(scratch);
        masm.push(Address(scratch, WasmActivation::offsetOfFP()));
#endif
        MOZ_ASSERT_IF(!masm.oom(), PushedFP == masm.currentOffset() - *entry);
        masm.storePtr(masm.getStackPointer(), Address(scratch, WasmActivation::offsetOfFP()));
        MOZ_ASSERT_IF(!masm.oom(), StoredFP == masm.currentOffset() - *entry);
    }

    if (reason != ExitReason::None)
        masm.store32(Imm32(int32_t(reason)), Address(scratch, WasmActivation::offsetOfExitReason()));
    if (framePushed)
        masm.subFromStackPtr(Imm32(framePushed));
#endif
}

// Generate the inverse of GenerateCallablePrologue.
static void
GenerateCallableEpilogue(MacroAssembler& masm, unsigned framePushed, ExitReason reason,
                         uint32_t* ret)
{
#if defined(JS_CODEGEN_X86)
    if (framePushed)
        masm.addToStackPtr(Imm32(framePushed));
    if (reason != ExitReason::None) {
        // A JS JIT callee may have clobbered every register. The exit's stack
        // header stays intact, so recover both physical FP and its TLS first.
        masm.moveStackPtrTo(FramePointer);
        masm.loadPtr(Address(FramePointer, FrameCalleeTLSOffset), WasmTlsReg);
        Register scratch = ABINonArgReturnReg0;
        masm.loadWasmActivationFromTls(scratch);
        masm.storePtr(ImmWord(0), Address(scratch, WasmActivation::offsetOfFP()));
        masm.store32(Imm32(int32_t(ExitReason::None)),
                     Address(scratch, WasmActivation::offsetOfExitReason()));
    }
    masm.pop(FramePointer);
    DebugOnly<uint32_t> poppedFP = masm.currentOffset();
    *ret = masm.currentOffset();
    MOZ_ASSERT_IF(!masm.oom(), PoppedFPToRet == *ret - poppedFP);
    masm.ret();
#else
    Register scratch = ABINonArgReturnReg0;
    Register scratch2 = ABINonArgReturnReg1;
    if (framePushed)
        masm.addToStackPtr(Imm32(framePushed));

    // Restore TLS from the fixed header, including import exits whose native
    // call or Ion callee was allowed to clobber every general register.
    masm.loadPtr(Address(masm.getStackPointer(), offsetof(Frame, tls)), WasmTlsReg);
    masm.loadWasmActivationFromTls(scratch);
    if (reason != ExitReason::None) {
        masm.store32(Imm32(int32_t(ExitReason::None)),
                     Address(scratch, WasmActivation::offsetOfExitReason()));
    }

#if defined(JS_CODEGEN_ARM)
    AutoForbidPools afp(&masm, /* number of instructions in scope = */ 5);
#endif
    // Publish the caller before releasing any field of the old frame. During
    // the following instruction the profiler must still read the full header
    // at SP, while activation.fp already points to the caller.
    masm.loadPtr(Address(masm.getStackPointer(), offsetof(Frame, callerFP)), scratch2);
    masm.storePtr(scratch2, Address(scratch, WasmActivation::offsetOfFP()));
    DebugOnly<uint32_t> prePop = masm.currentOffset();
    masm.addToStackPtr(Imm32(FrameBytesAfterReturnAddress));
    MOZ_ASSERT_IF(!masm.oom(), PostStorePrePopFP == masm.currentOffset() - prePop);
    *ret = masm.currentOffset();
    masm.ret();
#endif
}

void
wasm::GenerateFunctionPrologue(MacroAssembler& masm, unsigned framePushed, const SigIdDesc& sigId,
                               FuncOffsets* offsets)
{
#if defined(JS_CODEGEN_ARM)
    // Flush pending pools so they do not get dumped between the 'begin' and
    // 'normalEntry' offsets since the difference must be less than UINT8_MAX
    // to be stored in CodeRange::funcBeginToNormalEntry_.
    masm.flushBuffer();
#endif
    masm.haltingAlign(CodeAlignment);

    // Generate table entry:
    offsets->begin = masm.currentOffset();
    TrapOffset trapOffset(0);  // ignored by masm.wasmEmitTrapOutOfLineCode
    TrapDesc trap(trapOffset, Trap::IndirectCallBadSig, masm.framePushed());
    switch (sigId.kind()) {
      case SigIdDesc::Kind::Global: {
        Register scratch = WasmTableCallScratchReg;
        masm.loadWasmGlobalPtr(sigId.globalDataOffset(), scratch);
        masm.branchPtr(Assembler::Condition::NotEqual, WasmTableCallSigReg, scratch, trap);
        break;
      }
      case SigIdDesc::Kind::Immediate:
        masm.branch32(Assembler::Condition::NotEqual, WasmTableCallSigReg, Imm32(sigId.immediate()), trap);
        break;
      case SigIdDesc::Kind::None:
        break;
    }

    // Generate normal entry:
    masm.nopAlign(CodeAlignment);
    GenerateCallablePrologue(masm, framePushed, ExitReason::None, &offsets->normalEntry);

    masm.setFramePushed(framePushed);
}

void
wasm::GenerateFunctionEpilogue(MacroAssembler& masm, unsigned framePushed, FuncOffsets* offsets)
{
    MOZ_ASSERT(masm.framePushed() == framePushed);
    GenerateCallableEpilogue(masm, framePushed, ExitReason::None, &offsets->ret);
    masm.setFramePushed(0);
}

void
wasm::GenerateExitPrologue(MacroAssembler& masm, unsigned framePushed, ExitReason reason,
                           CallableOffsets* offsets)
{
    masm.haltingAlign(CodeAlignment);
    GenerateCallablePrologue(masm, framePushed, reason, &offsets->begin);
    masm.setFramePushed(framePushed);
}

void
wasm::GenerateExitEpilogue(MacroAssembler& masm, unsigned framePushed, ExitReason reason,
                           CallableOffsets* offsets)
{
    // Inverse of GenerateExitPrologue:
    MOZ_ASSERT(masm.framePushed() == framePushed);
    GenerateCallableEpilogue(masm, framePushed, reason, &offsets->ret);
    masm.setFramePushed(0);
}

/*****************************************************************************/
// ProfilingFrameIterator

ProfilingFrameIterator::ProfilingFrameIterator()
  : activation_(nullptr),
    code_(nullptr),
    codeRange_(nullptr),
    callerFP_(nullptr),
    callerPC_(nullptr),
    stackAddress_(nullptr),
    exitReason_(ExitReason::None)
{
    MOZ_ASSERT(done());
}

ProfilingFrameIterator::ProfilingFrameIterator(const WasmActivation& activation)
  : activation_(&activation),
    code_(nullptr),
    codeRange_(nullptr),
    callerFP_(nullptr),
    callerPC_(nullptr),
    stackAddress_(nullptr),
    exitReason_(ExitReason::None)
{
    initFromFP();
}

static inline void
AssertMatchesCallSite(const WasmActivation& activation, void* callerPC, void* callerFP)
{
#ifdef DEBUG
    Code* code = activation.compartment()->wasm.lookupCode(callerPC);
    MOZ_ASSERT(code);

    const CodeRange* callerCodeRange = code->lookupRange(callerPC);
    MOZ_ASSERT(callerCodeRange);

    if (callerCodeRange->kind() == CodeRange::Entry) {
        MOZ_ASSERT(callerFP == nullptr);
        return;
    }

    const CallSite* callsite = code->lookupCallSite(callerPC);
    (void)callsite; // Native builtin callouts may have no CallSite metadata.
#endif
}

void
ProfilingFrameIterator::initFromFP()
{
#if defined(JS_CODEGEN_X86)
    if (activation_->resumePC()) {
        uint8_t* fp = activation_->interruptFP();
        void* pc = activation_->interruptUnwindPC();
        if (!fp || !pc)
            return;
        code_ = activation_->compartment()->wasm.lookupCode(pc);
        if (!code_)
            return;
        codeRange_ = code_->lookupRange(pc);
        if (!codeRange_ || !codeRange_->isFunction()) {
            codeRange_ = nullptr;
            return;
        }
        stackAddress_ = fp;
        callerPC_ = ReturnAddressFromFP(fp);
        callerFP_ = CallerFPFromFP(fp);
        exitReason_ = ExitReason::Native;
        return;
    }
#endif
    uint8_t* fp = activation_->fp();
    stackAddress_ = fp;

    // If a signal was handled while entering an activation, the frame will
    // still be null.
    if (!fp) {
        MOZ_ASSERT(done());
        return;
    }

    void* pc = ReturnAddressFromFP(fp);

    code_ = activation_->compartment()->wasm.lookupCode(pc);
    MOZ_ASSERT(code_);

    codeRange_ = code_->lookupRange(pc);
    MOZ_ASSERT(codeRange_);

    // Since we don't have the pc for fp, start unwinding at the caller of fp
    // (ReturnAddressFromFP(fp)). This means that the innermost frame is
    // skipped. This is fine because:
    //  - for import exit calls, the innermost frame is a thunk, so the first
    //    frame that shows up is the function calling the import;
    //  - for Math and other builtin calls as well as interrupts, we note the absence
    //    of an exit reason and inject a fake "builtin" frame; and
    //  - for async interrupts, we just accept that we'll lose the innermost frame.
    switch (codeRange_->kind()) {
      case CodeRange::Entry:
        callerPC_ = nullptr;
        callerFP_ = nullptr;
        break;
      case CodeRange::Function:
        fp = CallerFPFromFP(fp);
        callerPC_ = ReturnAddressFromFP(fp);
        callerFP_ = CallerFPFromFP(fp);
        AssertMatchesCallSite(*activation_, callerPC_, callerFP_);
        break;
      case CodeRange::ImportJitExit:
      case CodeRange::ImportInterpExit:
      case CodeRange::TrapExit:
      case CodeRange::Inline:
      case CodeRange::FarJumpIsland:
        MOZ_CRASH("Unexpected CodeRange kind");
    }

    // The iterator inserts a pretend innermost frame for non-None ExitReasons.
    // This allows the variety of exit reasons to show up in the callstack.
    exitReason_ = activation_->exitReason();

    // In the case of calls to builtins or asynchronous interrupts, no exit path
    // is taken so the exitReason is None. Coerce these to the Native exit
    // reason so that self-time is accounted for.
    if (exitReason_ == ExitReason::None)
        exitReason_ = ExitReason::Native;

    MOZ_ASSERT(!done());
}

typedef JS::ProfilingFrameIterator::RegisterState RegisterState;

ProfilingFrameIterator::ProfilingFrameIterator(const WasmActivation& activation,
                                               const RegisterState& state)
  : activation_(&activation),
    code_(nullptr),
    codeRange_(nullptr),
    callerFP_(nullptr),
    callerPC_(nullptr),
    stackAddress_(nullptr),
    exitReason_(ExitReason::None)
{
#if defined(JS_CODEGEN_X86)
    // Published exits remain authoritative while a JS JIT callee can clobber
    // hardware FP. Interrupted code has a normalized snapshot of its own.
    if (activation.fp() || activation.resumePC()) {
        initFromFP();
        return;
    }
#endif
    // If pc isn't in the instance's code, we must have exited the code via an
    // exit trampoline or signal handler.
    code_ = activation_->compartment()->wasm.lookupCode(state.pc);
    if (!code_) {
#if defined(JS_CODEGEN_X86)
        void* callerPC;
        uint8_t* callerFP;
        void* stackAddress;
        if (UnwindBuiltinThunk(state.pc, state.sp, state.fp,
                               &callerPC, &callerFP, &stackAddress))
        {
            code_ = activation_->compartment()->wasm.lookupCode(callerPC);
            if (code_) {
                codeRange_ = code_->lookupRange(callerPC);
                if (codeRange_ && codeRange_->isFunction()) {
                    // The thunk has no per-compartment CodeRange. Attribute
                    // its current sample to a native call, then the actual
                    // calling Wasm function before proceeding to its caller.
                    stackAddress_ = stackAddress;
                    callerPC_ = ReturnAddressFromFP(callerFP);
                    callerFP_ = CallerFPFromFP(callerFP);
                    exitReason_ = ExitReason::Native;
                    return;
                }
                codeRange_ = nullptr;
            }
        }
#endif
        initFromFP();
        return;
    }

    // When the pc is inside the prologue/epilogue, the innermost call's Frame
    // is not complete and thus fp points to the second-to-innermost call's
    // Frame. Since fp can only tell you about its caller, naively unwinding
    // while pc is in the prologue/epilogue would skip the second-to-innermost
    // call. To avoid this problem, we use the static structure of the code in
    // the prologue and epilogue to do the Right Thing.
#if defined(JS_CODEGEN_X86)
    uint8_t* fp = static_cast<uint8_t*>(state.fp);
#else
    uint8_t* fp = activation.fp();
#endif
    uint8_t* pc = (uint8_t*)state.pc;
    void** sp = (void**)state.sp;

    const CodeRange* codeRange = code_->lookupRange(pc);
    if (!codeRange)
        return;
    uint32_t offsetInModule = pc - code_->segment().base();
    MOZ_ASSERT(offsetInModule >= codeRange->begin());
    MOZ_ASSERT(offsetInModule < codeRange->end());

    // Compute the offset of the pc from the (normal) entry of the code range.
    // The stack state of the pc for the entire table-entry is equivalent to
    // that of the first pc of the normal-entry. Thus, we can simplify the below
    // case analysis by redirecting all pc-in-table-entry cases to the
    // pc-at-normal-entry case.
    uint32_t offsetFromEntry;
    if (codeRange->isFunction()) {
        if (offsetInModule < codeRange->funcNormalEntry())
            offsetFromEntry = 0;
        else
            offsetFromEntry = offsetInModule - codeRange->funcNormalEntry();
    } else {
        offsetFromEntry = offsetInModule - codeRange->begin();
    }

    switch (codeRange->kind()) {
      case CodeRange::Function:
      case CodeRange::FarJumpIsland:
      case CodeRange::ImportJitExit:
      case CodeRange::ImportInterpExit:
      case CodeRange::TrapExit:
#if defined(JS_CODEGEN_ARM) || defined(JS_CODEGEN_ARM64) || \
    defined(JS_CODEGEN_MIPS32) || defined(JS_CODEGEN_MIPS64) || \
    defined(JS_CODEGEN_LOONGARCH64)
        if (offsetFromEntry < PushedRetAddr || codeRange->isThunk()) {
            callerPC_ = state.lr;
            callerFP_ = fp;
            AssertMatchesCallSite(*activation_, callerPC_, callerFP_);
        } else
#endif
        if (codeRange->isThunk()) {
            callerPC_ = *sp;
            callerFP_ = fp;
            AssertMatchesCallSite(*activation_, callerPC_, callerFP_);
#if defined(JS_CODEGEN_X86)
        } else if (offsetInModule >= codeRange->ret() - PoppedFPToRet &&
                   offsetInModule <= codeRange->ret()) {
            // FP already denotes the caller and SP points at the return
            // address; the compact epilogue has no TLS or padding pops.
            callerPC_ = sp[0];
            callerFP_ = fp;
            AssertMatchesCallSite(*activation_, callerPC_, callerFP_);
#else
        } else if (offsetInModule == codeRange->ret() - PostStorePrePopFP) {
            // activation.fp was restored, but SP still addresses the full Frame.
            callerPC_ = ReturnAddressFromFP(sp);
            callerFP_ = fp;
            AssertMatchesCallSite(*activation_, callerPC_, callerFP_);
#endif
        } else if (offsetInModule >= codeRange->ret() &&
                   offsetInModule < codeRange->ret() + ReturnBytes) {
            // Only the return address remains at SP.
#if defined(JS_CODEGEN_MIPS32) || defined(JS_CODEGEN_MIPS64) || \
    defined(JS_CODEGEN_LOONGARCH64)
            // ret expands to load RA, release its slot, then branch. Once the
            // slot is gone the saved link register, rather than SP, holds RA.
            callerPC_ = offsetInModule < codeRange->ret() + 8 ? *sp : state.lr;
#else
            callerPC_ = *sp;
#endif
            callerFP_ = fp;
            AssertMatchesCallSite(*activation_, callerPC_, callerFP_);
        } else if (offsetFromEntry < PushedFP) {
#if defined(JS_CODEGEN_MIPS32) || defined(JS_CODEGEN_MIPS64) || \
    defined(JS_CODEGEN_LOONGARCH64)
            // These prologues reserved the full header before storing RA.
            callerPC_ = ReturnAddressFromFP(sp);
#else
            // Each completed push/reservation adds one word before RA.
#if defined(JS_CODEGEN_X86)
            callerPC_ = *sp;
#else
            if (offsetFromEntry < PushedPadding)
                callerPC_ = *sp;
            else if (offsetFromEntry < PushedTLS)
                callerPC_ = sp[1];
            else
                callerPC_ = sp[2];
#endif
#endif
            callerFP_ = fp;
            AssertMatchesCallSite(*activation_, callerPC_, callerFP_);
        } else if (offsetFromEntry < StoredFP) {
            // The full Frame has been pushed; fp still points to the caller's
            // frame.
            MOZ_ASSERT(fp == CallerFPFromFP(sp));
            callerPC_ = ReturnAddressFromFP(sp);
            callerFP_ = CallerFPFromFP(sp);
            AssertMatchesCallSite(*activation_, callerPC_, callerFP_);
        } else {
            // Not in the prologue/epilogue.
#if defined(JS_CODEGEN_X86)
            if (!fp)
                return;
#endif
            callerPC_ = ReturnAddressFromFP(fp);
            callerFP_ = CallerFPFromFP(fp);
            AssertMatchesCallSite(*activation_, callerPC_, callerFP_);
        }
        break;
      case CodeRange::Entry:
        // The entry trampoline is the final frame in an WasmActivation. The entry
        // trampoline also doesn't GeneratePrologue/Epilogue so we can't use
        // the general unwinding logic above.
#if !defined(JS_CODEGEN_X86)
        MOZ_ASSERT(!fp);
#endif
        callerPC_ = nullptr;
        callerFP_ = nullptr;
        break;
      case CodeRange::Inline:
#if defined(JS_CODEGEN_X86)
        // Interrupts are handled from their captured state above. Other
        // inline stubs may be in the throw sequence after publication was
        // cleared, when a JS JIT callee's hardware FP cannot be trusted.
        return;
#endif
        // The throw stub clears WasmActivation::fp on it's way out.
        if (!fp) {
            MOZ_ASSERT(done());
            return;
        }

        // Most inline code stubs execute after the prologue/epilogue have
        // completed so we can simply unwind based on fp. The only exception is
        // the async interrupt stub, since it can be executed at any time.
        // However, the async interrupt is super rare, so we can tolerate
        // skipped frames. Thus, we use simply unwind based on fp.
        callerPC_ = ReturnAddressFromFP(fp);
        callerFP_ = CallerFPFromFP(fp);
        AssertMatchesCallSite(*activation_, callerPC_, callerFP_);
        break;
    }

    codeRange_ = codeRange;
    stackAddress_ = state.sp;
    MOZ_ASSERT(!done());
}

void
ProfilingFrameIterator::operator++()
{
    if (exitReason_ != ExitReason::None) {
        MOZ_ASSERT(codeRange_);
        exitReason_ = ExitReason::None;
        MOZ_ASSERT(!done());
        return;
    }

    if (!callerPC_) {
        MOZ_ASSERT(!callerFP_);
        codeRange_ = nullptr;
        MOZ_ASSERT(done());
        return;
    }

    code_ = activation_->compartment()->wasm.lookupCode(callerPC_);
    MOZ_ASSERT(code_);

    codeRange_ = code_->lookupRange(callerPC_);
    MOZ_ASSERT(codeRange_);

    switch (codeRange_->kind()) {
      case CodeRange::Entry:
        MOZ_ASSERT(callerFP_ == nullptr);
        callerPC_ = nullptr;
        break;
      case CodeRange::Function:
      case CodeRange::ImportJitExit:
      case CodeRange::ImportInterpExit:
      case CodeRange::TrapExit:
      case CodeRange::Inline:
      case CodeRange::FarJumpIsland:
        stackAddress_ = callerFP_;
        callerPC_ = ReturnAddressFromFP(callerFP_);
        AssertMatchesCallSite(*activation_, callerPC_, CallerFPFromFP(callerFP_));
        callerFP_ = CallerFPFromFP(callerFP_);
        break;
    }

    MOZ_ASSERT(!done());
}

const char*
ProfilingFrameIterator::label() const
{
    MOZ_ASSERT(!done());

    // Use the same string for both time inside and under so that the two
    // entries will be coalesced by the profiler.
    //
    // NB: these labels are parsed for location by
    //     devtools/client/performance/modules/logic/frame-utils.js
    const char* importJitDescription = "fast FFI trampoline (in asm.js)";
    const char* importInterpDescription = "slow FFI trampoline (in asm.js)";
    const char* nativeDescription = "native call (in asm.js)";
    const char* trapDescription = "trap handling (in asm.js)";

    switch (exitReason_) {
      case ExitReason::None:
        break;
      case ExitReason::ImportJit:
        return importJitDescription;
      case ExitReason::ImportInterp:
        return importInterpDescription;
      case ExitReason::Native:
        return nativeDescription;
      case ExitReason::Trap:
        return trapDescription;
    }

    switch (codeRange_->kind()) {
      case CodeRange::Function:         return code_->profilingLabel(codeRange_->funcIndex());
      case CodeRange::Entry:            return "entry trampoline (in asm.js)";
      case CodeRange::ImportJitExit:    return importJitDescription;
      case CodeRange::ImportInterpExit: return importInterpDescription;
      case CodeRange::TrapExit:         return trapDescription;
      case CodeRange::Inline:           return "inline stub (in asm.js)";
      case CodeRange::FarJumpIsland:    return "interstitial (in asm.js)";
    }

    MOZ_CRASH("bad code range kind");
}


Instance*
wasm::LookupFaultingInstance(WasmActivation* activation, void* pc, void* physicalFP)
{
    const Code* code = activation->compartment()->wasm.lookupCode(pc);
    if (!code || !code->segment().containsFunctionPC(pc))
        return nullptr;
    const CodeRange* range = code->lookupRange(pc);
    if (!range || !range->isFunction())
        return nullptr;
    size_t offset = static_cast<uint8_t*>(pc) - code->segment().base();
    if (offset < range->funcNormalEntry() + StoredFP ||
        (offset >= range->ret() - PostStorePrePopFP && offset < range->ret() + ReturnBytes))
    {
        return nullptr;
    }
#ifndef WASM_HUGE_MEMORY
    // A real heap operation has exact metadata. Stack spills and header loads
    // do not, so an unrelated bug cannot reinterpret a caller/null Frame as
    // the current instance. Huge-memory wasm deliberately omits this table.
    if (!code->lookupMemoryAccess(pc))
        return nullptr;
#endif
#if defined(JS_CODEGEN_X86)
    TlsData* tls = FrameTLS(*activation, physicalFP, pc);
#else
    Frame* frame = reinterpret_cast<Frame*>(activation->fp());
    TlsData* tls = frame ? frame->tls : nullptr;
#endif
    if (!tls)
        return nullptr;
    Instance* instance = tls->instance;
    if (!instance || &instance->code() != code)
        return nullptr;
    return instance;
}

TlsData*
wasm::FrameTLS(const WasmActivation& activation, void* rawFrame, void* pc)
{
#if defined(JS_CODEGEN_X86)
    Frame* frame = static_cast<Frame*>(rawFrame);
    // Instance-switch slots belong to the called frame. Internal calls do not
    // write them; follow the physical chain to the latest actual switch.
    // A compact recursive frame can legitimately exceed 100,000 links within
    // the embedding's stack quota. Bound traversal by its strictly increasing
    // physical addresses and actual entry/switch, never an arbitrary count.
    while (frame) {
        void* returnPC = frame->returnAddress;
        const Code* callerCode = activation.compartment()->wasm.lookupCode(returnPC);
        if (!callerCode)
            return nullptr;
        const CallSite* call = callerCode->lookupCallSite(returnPC);
        const CodeRange* callerRange = callerCode->lookupRange(returnPC);
        if ((call && call->mightSwitchInstance()) ||
            (callerRange && callerRange->kind() == CodeRange::Entry)) {
            TlsData* tls = static_cast<FrameWithInstances*>(frame)->calleeTLS;
            if (pc && tls) {
                const Code* currentCode = activation.compartment()->wasm.lookupCode(pc);
                if (!currentCode || !tls->instance || &tls->instance->code() != currentCode)
                    return nullptr;
            }
            return tls;
        }
        Frame* caller = reinterpret_cast<Frame*>(frame->callerFP);
        if (caller && uintptr_t(caller) <= uintptr_t(frame))
            return nullptr;
        frame = caller;
    }
    return nullptr;
#else
    Frame* frame = static_cast<Frame*>(rawFrame);
    return frame ? frame->tls : nullptr;
#endif
}

TlsData*
wasm::LookupFrameTLS(void* frame)
{
    return FrameTLS(*JSRuntime::innermostWasmActivation(), frame);
}

bool
wasm::StartInterrupt(WasmActivation* activation, void* rawPC, void* rawSP, void* rawFP)
{
#if defined(JS_CODEGEN_X86)
    const Code* code = activation->compartment()->wasm.lookupCode(rawPC);
    if (!code)
        return false;
    const CodeRange* range = code->lookupRange(rawPC);
    if (!range || !range->isFunction())
        return false;
    uint8_t* pc = static_cast<uint8_t*>(rawPC);
    uint8_t* fp = static_cast<uint8_t*>(rawFP);
    void** sp = static_cast<void**>(rawSP);
    uint32_t offset = pc - code->segment().base();
    uint32_t fromEntry = offset < range->funcNormalEntry()
                         ? 0 : offset - range->funcNormalEntry();
    if (offset >= range->ret() - PoppedFPToRet && offset <= range->ret()) {
        pc = static_cast<uint8_t*>(sp[0]);
    } else if (fromEntry < StoredFP) {
        unsigned returnSlot = fromEntry < PushedFP ? 0 : 1;
        pc = static_cast<uint8_t*>(sp[returnSlot]);
    }
    activation->startInterrupt(rawPC, pc, fp);
#else
    activation->setResumePC(rawPC);
#endif
    return true;
}
