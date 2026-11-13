/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef MOZILLA_LAYERS_DIBTEXTURECODEC_H
#define MOZILLA_LAYERS_DIBTEXTURECODEC_H

#include <stddef.h>
#include <stdint.h>

namespace mozilla {
namespace layers {
namespace DIBTextureCodec {

// Lossless, bounded encoding of complete 32-bit pixels, including the unused
// byte of X8 formats. A 16-bit tag holds a length minus one and a run/literal
// bit. Runs carry one pixel; literal spans carry every pixel. All accesses
// accept unaligned input/output. This is private same-process storage, not a
// persistent or wire format. Zero means failure; callers can retain raw data.
size_t Encode(const uint8_t* aInput, size_t aBytes,
              uint8_t* aOutput, size_t aCapacity);
bool Decode(const uint8_t* aInput, size_t aBytes,
            uint8_t* aOutput, size_t aOutputBytes);

} // namespace DIBTextureCodec
} // namespace layers
} // namespace mozilla
#endif
