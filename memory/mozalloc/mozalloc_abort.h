/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_mozalloc_abort_h
#define mozilla_mozalloc_abort_h

#include "mozilla/Attributes.h"
#include "mozilla/Types.h"

/**
 * Terminate this process in such a way that breakpad is triggered, if
 * at all possible.
 *
 * Note: MOZ_NORETURN seems to break crash stacks on ARM, so we don't
 * use that annotation there.
 */
#ifdef __cplusplus
/* This header can be reached from inside an extern "C" block (for example via
   <rpc.h> -> <windows.h> -> <stdlib.h> -> the STL wrappers). Without an
   explicit C++ linkage specification the symbol would be emitted unmangled and
   fail to resolve against the C++-mangled export in mozglue. */
extern "C++" {
#endif

MFBT_API
#if !defined(__arm__)
  MOZ_NORETURN
#endif
  void mozalloc_abort(const char* const msg);

#ifdef __cplusplus
}
#endif


#endif  /* ifndef mozilla_mozalloc_abort_h */
