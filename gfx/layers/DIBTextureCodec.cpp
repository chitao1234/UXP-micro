/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "DIBTextureCodec.h"
#include <string.h>
#if defined(__SSE2__) || defined(_M_X64) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define DIB_CODEC_SSE2
#endif

namespace mozilla {
namespace layers {
namespace DIBTextureCodec {

static uint32_t Pixel(const uint8_t* aInput, size_t aIndex)
{
  uint32_t value;
  memcpy(&value, aInput + aIndex * 4, sizeof(value));
  return value;
}

size_t Encode(const uint8_t* aInput, size_t aBytes,
              uint8_t* aOutput, size_t aCapacity)
{
  if (!aBytes || aBytes % 4) return 0;
  size_t pixels = aBytes / 4, i = 0, out = 0;
  while (i < pixels) {
    size_t run = 1;
    uint32_t value = Pixel(aInput, i);
    if (pixels - i >= 3 && Pixel(aInput, i + 1) == value &&
        Pixel(aInput, i + 2) == value) {
      run = 3;
#ifdef DIB_CODEC_SSE2
      // Flat raster regions are the target. Scan four complete pixels per
      // comparison, using only the SSE2 already required by SSE2 builds.
      __m128i repeated = _mm_set1_epi32(int32_t(value));
      while (run <= 32764 && pixels - i - run >= 4) {
        __m128i next = _mm_loadu_si128(
          reinterpret_cast<const __m128i*>(aInput + (i + run) * 4));
        if (_mm_movemask_epi8(_mm_cmpeq_epi32(next, repeated)) != 0xffff) break;
        run += 4;
      }
#endif
      while (run < 32768 && run < pixels - i &&
             Pixel(aInput, i + run) == value) ++run;
    }
    uint16_t tag;
    if (run >= 3) {
      if (aCapacity - out < 6) return 0;
      tag = uint16_t(0x8000u | (run - 1));
      memcpy(aOutput + out, &tag, 2);
      memcpy(aOutput + out + 2, &value, 4);
      out += 6;
      i += run;
    } else {
      size_t begin = i++;
      while (i < pixels && i - begin < 32768) {
        if (pixels - i >= 3 && Pixel(aInput, i) == Pixel(aInput, i + 1) &&
            Pixel(aInput, i) == Pixel(aInput, i + 2)) break;
        ++i;
      }
      size_t bytes = (i - begin) * 4;
      if (aCapacity - out < 2 || bytes > aCapacity - out - 2) return 0;
      tag = uint16_t(i - begin - 1);
      memcpy(aOutput + out, &tag, 2);
      memcpy(aOutput + out + 2, aInput + begin * 4, bytes);
      out += 2 + bytes;
    }
  }
  return out;
}

bool Decode(const uint8_t* aInput, size_t aBytes,
            uint8_t* aOutput, size_t aOutputBytes)
{
  if (!aOutputBytes || aOutputBytes % 4) return false;
  size_t input = 0, out = 0;
  while (input < aBytes) {
    if (aBytes - input < 2) return false;
    uint16_t tag;
    memcpy(&tag, aInput + input, 2);
    input += 2;
    size_t count = (tag & 0x7fffu) + 1;
    if (count > (aOutputBytes - out) / 4) return false;
    if (tag & 0x8000u) {
      if (aBytes - input < 4) return false;
      uint32_t value;
      memcpy(&value, aInput + input, 4);
      input += 4;
      size_t j = 0;
#ifdef DIB_CODEC_SSE2
      __m128i repeated = _mm_set1_epi32(int32_t(value));
      for (; count - j >= 4; j += 4)
        _mm_storeu_si128(reinterpret_cast<__m128i*>(aOutput + out + j * 4), repeated);
#endif
      for (; j < count; ++j)
        memcpy(aOutput + out + j * 4, &value, 4);
    } else {
      if (count > (aBytes - input) / 4) return false;
      memcpy(aOutput + out, aInput + input, count * 4);
      input += count * 4;
    }
    out += count * 4;
  }
  return out == aOutputBytes;
}

} // namespace DIBTextureCodec
} // namespace layers
} // namespace mozilla

#undef DIB_CODEC_SSE2
