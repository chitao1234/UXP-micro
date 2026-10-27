/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef mozilla_dom_AbortSignalTimeout_h
#define mozilla_dom_AbortSignalTimeout_h
#include <stdint.h>
namespace mozilla { namespace dom {
// Preserve uint64 WebIDL deadlines even when the native timer accepts only a
// bounded signed interval. Rearm only when a complete active-time slice passed.
inline int32_t TakeAbortTimeoutSlice(uint64_t& aRemaining, uint32_t aLimit) {
  uint32_t delay = aRemaining > aLimit ? aLimit : uint32_t(aRemaining);
  aRemaining -= delay;
  return int32_t(delay);
}
} }
#endif
