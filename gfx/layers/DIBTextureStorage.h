/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef MOZILLA_LAYERS_DIBTEXTURESTORAGE_H
#define MOZILLA_LAYERS_DIBTEXTURESTORAGE_H

#include "mozilla/Mutex.h"
#include "mozilla/TimeStamp.h"
#include "nsISupportsImpl.h"
#include "nsTArray.h"
#include "gfxWindowsSurface.h"

namespace mozilla {
namespace layers {

// Same-process DIB backing shared by the producer and compositor. A successful
// Pin owns both a counted lease and a surface reference until Unpin. Cached
// source views additionally own a surface reference, so aliases prevent release.
class DIBTextureStorage final
{
public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(DIBTextureStorage)
  explicit DIBTextureStorage(gfxWindowsSurface* aSurface);

  already_AddRefed<gfxWindowsSurface> Pin();
  void Unpin();
  gfx::IntSize Size() const { return mSize; }
  gfx::SurfaceFormat Format() const { return mFormat; }

  // Negative means ineligible. One successful compression/restoration at most
  // per backing: an interrupted attempt or a restored texture stays raw for
  // its remaining lifetime. Hot content therefore cannot cause codec cycling.
  int32_t IdleWaitMs(uint32_t aDelayMs);
  enum class Result { Stopped, More, Compressed };
  Result CompressOneBlock();
  void CancelCompression();
  bool IsCompressed();
  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf);

  static void InitializePolicy();
  static void ShutdownPolicy();
  static uint32_t QuietDelayMs();

private:
  ~DIBTextureStorage();
  void CancelLocked();
  bool RestoreLocked();

  Mutex mMutex;
  RefPtr<gfxWindowsSurface> mSurface;
  const gfx::IntSize mSize;
  const gfx::SurfaceFormat mFormat;
  TimeStamp mLastUse;
  nsTArray<uint8_t> mEncoded;
  size_t mOffset = 0;
  size_t mRawBytes = 0;
  double mCodecUsec = 0;
  int32_t mStride = 0;
  uint32_t mPins = 0;
  bool mPermanentlyRaw = false;

  DIBTextureStorage* mNext = nullptr;
  DIBTextureStorage** mPrevious = nullptr;
  friend class DIBTextureStorageReporter;
};

} // namespace layers
} // namespace mozilla
#endif
