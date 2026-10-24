/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * SQLite uses malloc_usable_size when built with jemalloc, but only gets an
 * implicit declaration under MSVC (C4013 is suppressed for exactly this case).
 * GCC treats that as an error, so declare it explicitly. The symbol is
 * provided by mozglue (mozmemory_wrap.c) and links into mozsqlite3.dll.
 */

#ifndef MINGW_MALLOC_USABLE_SIZE_H
#define MINGW_MALLOC_USABLE_SIZE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t malloc_usable_size(const void* ptr);

#ifdef __cplusplus
}
#endif

#endif /* MINGW_MALLOC_USABLE_SIZE_H */
