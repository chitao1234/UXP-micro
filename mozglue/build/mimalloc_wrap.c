/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * MinGW/mimalloc experiment.
 *
 * When jemalloc is the allocator, memory/build/mozmemory_wrap.c provides the
 * wrap_strdup/wrap_wcsdup/_aligned_malloc entry points that mozglue.dll
 * re-exports under their unprefixed names. memory/build is only built when
 * MOZ_MEMORY is set, so a mimalloc build has to provide the same entry points
 * against mi_* instead. They must come from the same allocator as
 * malloc/free: a string allocated by msvcrt's strdup and freed by a Gecko
 * module's free() (which resolves to mozglue) is the cross-allocator bug this
 * build is trying to avoid.
 */

#include <stddef.h>
#include <string.h>
#include <wchar.h>

#include "mimalloc.h"

#undef strdup
#undef wcsdup

char*
wrap_strndup(const char* src, size_t len)
{
  char* dst = (char*)mi_malloc(len + 1);
  if (dst) {
    memcpy(dst, src, len);
    dst[len] = '\0';
  }
  return dst;
}

char*
wrap_strdup(const char* src)
{
  return wrap_strndup(src, strlen(src));
}

wchar_t*
wrap_wcsdup(const wchar_t* src)
{
  size_t len = wcslen(src);
  wchar_t* dst = (wchar_t*)mi_malloc((len + 1) * sizeof(wchar_t));
  if (dst) {
    memcpy(dst, src, (len + 1) * sizeof(wchar_t));
  }
  return dst;
}

void*
_aligned_malloc(size_t size, size_t alignment)
{
  return mi_malloc_aligned(size, alignment);
}

/*
 * Jemalloc's mozmemory_wrap.c exports this no-op as "frex" to work around an
 * old CRT shutdown path. Keep the export for parity; nothing in this tree
 * references frex(), so it is only a safety valve.
 */
void
dumb_free_thunk(void* ptr)
{
  (void)ptr;
}
