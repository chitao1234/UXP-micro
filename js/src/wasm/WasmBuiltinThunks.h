/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef wasm_builtin_thunks_h
#define wasm_builtin_thunks_h

#include <stddef.h>
#include <stdint.h>

namespace js {
namespace wasm {

enum class SymbolicAddress;

// Native x86 helpers retain their existing system ABI, including x87 results.
// Wrappers publish a proper exit frame while the native function is executing.
bool EnsureBuiltinThunksInitialized();
void ReleaseBuiltinThunks();
void* SymbolicAddressTarget(SymbolicAddress address);

// Signal/profiler-safe lookup: immutable metadata published with the code.
bool LookupBuiltinThunk(void* pc);
bool UnwindBuiltinThunk(void* pc, void* sp, void* physicalFP,
                        void** callerPC, uint8_t** callerFP, void** stackAddress);
size_t BuiltinThunksCodeSize();

} // namespace wasm
} // namespace js

#endif // wasm_builtin_thunks_h
