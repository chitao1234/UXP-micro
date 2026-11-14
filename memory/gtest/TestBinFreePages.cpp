/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "jemalloc_bin_free_pages.h"
#include "mozmemory.h"
#include <vector>

TEST(BinFreePages, CensusIsSeparateFromRoutineHeapQueries)
{
  jemalloc_stats_t stats;
  stats.bin_unused_pagewise = stats.bin_unused_runs =
    stats.bin_unused_runs_with_free_pages = SIZE_MAX;
  jemalloc_stats(&stats);
  EXPECT_EQ(size_t(0), stats.bin_unused_pagewise);
  EXPECT_EQ(size_t(0), stats.bin_unused_runs);
  EXPECT_EQ(size_t(0), stats.bin_unused_runs_with_free_pages);

  jemalloc_stats_with_bin_census(&stats);
  EXPECT_LE(stats.bin_unused_pagewise, stats.bin_unused);
  EXPECT_LE(stats.bin_unused_runs_with_free_pages, stats.bin_unused_runs);
}

TEST(BinFreePages, ExcludesHeaderPages)
{
  unsigned masks[] = {UINT_MAX};
  EXPECT_EQ(size_t(192),
    jemalloc_run_free_page_bytes(masks, 20, 12, 16, 256, 64));
  EXPECT_EQ(size_t(0),
    jemalloc_run_free_page_bytes(masks, 4, 12, 16, 64, 64));
}

TEST(BinFreePages, LiveRegionCrossingBoundaryPinsBothPages)
{
  // Region 9 occupies offsets [124,136), crossing the boundary at 128.
  unsigned masks[] = {UINT_MAX & ~(1u << 9)};
  EXPECT_EQ(size_t(64),
    jemalloc_run_free_page_bytes(masks, 20, 12, 16, 256, 64));
}

TEST(BinFreePages, BitmapWordBoundaries)
{
  unsigned masks[] = {UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX};
  EXPECT_EQ(size_t(256),
    jemalloc_run_free_page_bytes(masks, 128, 2, 0, 256, 64));
  masks[1] &= ~1u; // Only the second page is pinned by region 32.
  EXPECT_EQ(size_t(192),
    jemalloc_run_free_page_bytes(masks, 128, 2, 0, 256, 64));
  masks[2] &= ~(1u << 31); // Region 95 pins only the third page.
  EXPECT_EQ(size_t(128),
    jemalloc_run_free_page_bytes(masks, 128, 2, 0, 256, 64));
}

TEST(BinFreePages, IgnoresPaddingBitsAndMetadataSpanningPages)
{
  // Metadata ends at offset 100. The 39 regions fill the run through 256.
  // Unused bits above the last region are zero and must not pin any page.
  unsigned masks[] = {UINT_MAX, (1u << 7) - 1};
  EXPECT_EQ(size_t(128),
    jemalloc_run_free_page_bytes(masks, 39, 4, 100, 256, 64));
  masks[0] &= ~(1u << 24);
  EXPECT_EQ(size_t(64),
    jemalloc_run_free_page_bytes(masks, 39, 4, 100, 256, 64));
}

TEST(BinFreePages, FullRunHasNoFreePages)
{
  unsigned masks[] = {0};
  EXPECT_EQ(size_t(0),
    jemalloc_run_free_page_bytes(masks, 20, 12, 16, 256, 64));
}

TEST(BinFreePages, MatchesIndependentLiveRegionPageOracle)
{
  const size_t regions[] = {4, 8, 12, 24, 48, 96, 192, 256, 768, 1536, 2048};
  const size_t pageSize = 4096;
  const size_t wordBits = sizeof(unsigned) * CHAR_BIT;
  unsigned random = 387641;
  for (size_t regionSize : regions) {
    for (size_t pages = 1; pages <= 8; ++pages) {
      size_t runSize = pages * pageSize;
      size_t count = (runSize - 256) / regionSize;
      size_t first = runSize - count * regionSize;
      for (size_t trial = 0; trial < 12; ++trial) {
        std::vector<unsigned> masks((count + wordBits - 1) / wordBits, UINT_MAX);
        std::vector<bool> pinned(pages, false);
        // Mark metadata, then every byte range owned by a live region. This
        // oracle does not calculate intersecting region-index ranges per page.
        for (size_t offset = 0; offset < first; offset += pageSize)
          pinned[offset / pageSize] = true;
        for (size_t region = 0; region < count; ++region) {
          random = random * 1664525u + 1013904223u;
          bool live = trial == 0 || (trial > 1 && random % 97 < trial);
          if (!live)
            continue;
          masks[region / wordBits] &= ~(1u << (region % wordBits));
          size_t start = first + region * regionSize;
          size_t end = start + regionSize - 1;
          for (size_t page = start / pageSize; page <= end / pageSize; ++page)
            pinned[page] = true;
        }
        size_t expected = 0;
        for (bool live : pinned)
          if (!live)
            expected += pageSize;
        EXPECT_EQ(expected, jemalloc_run_free_page_bytes(masks.data(), count,
          regionSize, first, runSize, pageSize))
          << "region_size=" << regionSize << " pages=" << pages
          << " trial=" << trial;
      }
    }
  }
}
