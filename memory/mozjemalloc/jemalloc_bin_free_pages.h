/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jemalloc_bin_free_pages_h
#define jemalloc_bin_free_pages_h

#include <limits.h>
#include <stddef.h>

/*
 * Non-mutating census for an allocated small-object run. A set bitmap bit
 * denotes a free region. Exclude all pages intersecting the run header and
 * require every region intersecting a payload page to be free, including
 * regions straddling its boundaries. The caller holds the run's arena lock.
 * This is a geometric upper bound, not a decommit operation or policy.
 */
static inline size_t
jemalloc_run_free_page_bytes(const unsigned* masks, size_t nregs,
                            size_t region_size, size_t first_region,
                            size_t run_size, size_t page_size)
{
  const size_t word_bits = sizeof(unsigned) * CHAR_BIT;
  size_t page, bytes = 0;

  for (page = ((first_region + page_size - 1) / page_size) * page_size;
       page + page_size <= run_size; page += page_size) {
    size_t first = (page - first_region) / region_size;
    size_t end = (page + page_size - first_region + region_size - 1) /
                 region_size;
    size_t region;
    if (end > nregs)
      end = nregs;

    for (region = first; region < end; ) {
      size_t bit = region % word_bits;
      size_t count = word_bits - bit;
      unsigned mask;
      if (count > end - region)
        count = end - region;
      /* A full word shifts right by zero, never by its bit width. */
      mask = (UINT_MAX >> (word_bits - count)) << bit;
      if ((masks[region / word_bits] & mask) != mask)
        break;
      region += count;
    }
    if (region == end)
      bytes += page_size;
  }
  return bytes;
}

#endif
