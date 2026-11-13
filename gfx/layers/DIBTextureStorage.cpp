/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "DIBTextureStorage.h"
#include "DIBTextureCodec.h"
#include "gfx2DGlue.h"
#include "gfxImageSurface.h"
#include "mozilla/Atomics.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticMutex.h"
#include "nsIMemoryReporter.h"
#include "nsThreadUtils.h"
#include "cairo-win32.h"
#include <algorithm>
#include <cmath>

namespace mozilla {
namespace layers {

using namespace gfx;

static StaticMutex sDIBStorageMutex;
static DIBTextureStorage* sDIBStorage;
static Atomic<uint32_t, Relaxed> sColdDelay(0);
static Atomic<uint64_t, Relaxed> sCompressedCount(0), sRestoredCount(0);
static Atomic<uint64_t, Relaxed> sCompressedRawBytes(0), sCompressionUsec(0), sRestoreUsec(0);
static Atomic<uint64_t, Relaxed> sCanceledCount(0), sRejectedCount(0);
static Atomic<uint64_t, Relaxed> sAttemptCount(0), sBudgetRejectedCount(0);
static Atomic<uint64_t, Relaxed> sAliasRejectedCount(0), sAllocationRejectedCount(0);
static Atomic<uint64_t, Relaxed> sRatioRejectedCount(0);
static bool sColdPolicyInitialized;
static const size_t ColdBlockSize = 64 * 1024;
MOZ_DEFINE_MALLOC_SIZE_OF(ColdDIBMallocSizeOf)

static void ColdDIBPolicyChanged(const char*, void*)
{
  MOZ_ASSERT(NS_IsMainThread());
  // Opt-in until native pixels, interaction CPU and real-memory gates pass.
  bool enabled = Preferences::GetBool("browser.memory.compressColdTextures", false);
  uint32_t delay = Preferences::GetUint("browser.memory.coldTextureDelayMs", 10000);
  sColdDelay = enabled ? std::min(uint32_t(60000), std::max(uint32_t(1000), delay)) : 0;
}

class DIBTextureStorageReporter final : public nsIMemoryReporter
{
  ~DIBTextureStorageReporter() = default;
public:
  NS_DECL_ISUPPORTS
  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool) override
  {
    size_t heap = 0, compressed = 0, raw = 0, count = 0;
    {
      // Storage methods never take the registry mutex. Destruction unlinks
      // before member cleanup; the report cannot race a deleted owner.
      StaticMutexAutoLock lock(sDIBStorageMutex);
      for (auto* storage = sDIBStorage; storage; storage = storage->mNext) {
        heap += storage->SizeOfIncludingThis(ColdDIBMallocSizeOf);
        count++;
        MutexAutoLock state(storage->mMutex);
        if (!storage->mSurface && !storage->mEncoded.IsEmpty()) {
          compressed += storage->mEncoded.Length();
          raw += storage->mRawBytes;
        }
      }
    }
    MOZ_COLLECT_REPORT("explicit/gfx/cold-dib-storage", KIND_HEAP, UNITS_BYTES,
      heap, "Same-process DIB backing owners and compressed/in-progress buffers, including allocation capacity. Raw DIB pixels stay in the Cairo backing-owner report.");
    MOZ_COLLECT_REPORT("gfx-cold-textures/owners", KIND_OTHER, UNITS_COUNT, count,
      "Same-process shared DIB backing owners.");
    MOZ_COLLECT_REPORT("gfx-cold-textures/compressed-payload", KIND_OTHER, UNITS_BYTES, compressed,
      "Current compressed payload; overlaps explicit/gfx/cold-dib-storage.");
    MOZ_COLLECT_REPORT("gfx-cold-textures/compressed-raw-size", KIND_OTHER, UNITS_BYTES, raw,
      "Original size of current compressed DIBs; absent from live raw DIB backing. Not additional live memory.");
    MOZ_COLLECT_REPORT("gfx-cold-textures/compressions", KIND_OTHER, UNITS_COUNT, sCompressedCount,
      "Completed cold DIB compressions.");
    MOZ_COLLECT_REPORT("gfx-cold-textures/restores", KIND_OTHER, UNITS_COUNT, sRestoredCount,
      "Restorations into new DIBs. A restored backing stays raw thereafter.");
    MOZ_COLLECT_REPORT("gfx-cold-textures/compressed-bytes-total", KIND_OTHER, UNITS_BYTES, sCompressedRawBytes,
      "Cumulative raw bytes compressed, not live memory.");
    MOZ_COLLECT_REPORT("gfx-cold-textures/compress-microseconds", KIND_OTHER, UNITS_COUNT, sCompressionUsec,
      "Cumulative wall microseconds in compression slices; includes scheduling interruptions within a slice.");
    MOZ_COLLECT_REPORT("gfx-cold-textures/restore-microseconds", KIND_OTHER, UNITS_COUNT, sRestoreUsec,
      "Cumulative wall microseconds restoring DIBs.");
    MOZ_COLLECT_REPORT("gfx-cold-textures/canceled", KIND_OTHER, UNITS_COUNT, sCanceledCount,
      "Partial compressions interrupted by use or policy changes; backing stays raw.");
    MOZ_COLLECT_REPORT("gfx-cold-textures/rejected", KIND_OTHER, UNITS_COUNT, sRejectedCount,
      "Compression attempts rejected for aliases, CPU budget, insufficient saving or allocation failure.");
    MOZ_COLLECT_REPORT("gfx-cold-textures/attempts", KIND_OTHER, UNITS_COUNT, sAttemptCount,
      "Cold backings examined for compression, including alias rejections.");
    MOZ_COLLECT_REPORT("gfx-cold-textures/rejected-budget", KIND_OTHER, UNITS_COUNT, sBudgetRejectedCount,
      "Backings left raw because cumulative elapsed encoding time exceeded four milliseconds.");
    MOZ_COLLECT_REPORT("gfx-cold-textures/rejected-alias", KIND_OTHER, UNITS_COUNT, sAliasRejectedCount,
      "Backings left raw because an external Cairo or image reference still owns the pixels.");
    MOZ_COLLECT_REPORT("gfx-cold-textures/rejected-allocation", KIND_OTHER, UNITS_COUNT, sAllocationRejectedCount,
      "Backings left raw because encoded-buffer allocation failed.");
    MOZ_COLLECT_REPORT("gfx-cold-textures/rejected-ratio", KIND_OTHER, UNITS_COUNT, sRatioRejectedCount,
      "Backings left raw because encoded storage exceeded three quarters of their raw size.");
    return NS_OK;
  }
};
NS_IMPL_ISUPPORTS(DIBTextureStorageReporter, nsIMemoryReporter)

void DIBTextureStorage::InitializePolicy()
{
  MOZ_ASSERT(NS_IsMainThread());
  if (sColdPolicyInitialized) return;
  sColdPolicyInitialized = true;
  Preferences::RegisterCallback(ColdDIBPolicyChanged, "browser.memory.compressColdTextures");
  Preferences::RegisterCallback(ColdDIBPolicyChanged, "browser.memory.coldTextureDelayMs");
  ColdDIBPolicyChanged(nullptr, nullptr);
  RegisterStrongMemoryReporter(new DIBTextureStorageReporter());
}

void DIBTextureStorage::ShutdownPolicy()
{
  MOZ_ASSERT(NS_IsMainThread());
  if (!sColdPolicyInitialized) return;
  Preferences::UnregisterCallback(ColdDIBPolicyChanged, "browser.memory.compressColdTextures");
  Preferences::UnregisterCallback(ColdDIBPolicyChanged, "browser.memory.coldTextureDelayMs");
  sColdDelay = 0;
  sColdPolicyInitialized = false;
}

uint32_t DIBTextureStorage::QuietDelayMs() { return sColdDelay; }

DIBTextureStorage::DIBTextureStorage(gfxWindowsSurface* aSurface)
  : mMutex("DIBTextureStorage"), mSurface(aSurface),
    mSize(aSurface->GetSize()), mFormat(aSurface->GetSurfaceFormat()),
    mLastUse(TimeStamp::Now())
{
  cairo_surface_t* image = cairo_win32_surface_get_image(aSurface->CairoSurface());
  if (image) {
    mStride = cairo_image_surface_get_stride(image);
    mRawBytes = size_t(mStride) * size_t(mSize.height);
  }
  StaticMutexAutoLock lock(sDIBStorageMutex);
  mNext = sDIBStorage;
  mPrevious = &sDIBStorage;
  if (mNext) mNext->mPrevious = &mNext;
  sDIBStorage = this;
}

DIBTextureStorage::~DIBTextureStorage()
{
  MOZ_ASSERT(!mPins);
  StaticMutexAutoLock lock(sDIBStorageMutex);
  *mPrevious = mNext;
  if (mNext) mNext->mPrevious = mPrevious;
}

size_t DIBTextureStorage::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf)
{
  MutexAutoLock lock(mMutex);
  return aMallocSizeOf(this) + mEncoded.ShallowSizeOfExcludingThis(aMallocSizeOf);
}

void DIBTextureStorage::CancelLocked()
{
  if (mOffset && mSurface) {
    mEncoded.ClearAndRetainStorage();
    mEncoded.Compact();
    mOffset = 0;
    mPermanentlyRaw = true;
    ++sCanceledCount;
  }
}

void DIBTextureStorage::CancelCompression()
{
  MutexAutoLock lock(mMutex);
  CancelLocked();
}

already_AddRefed<gfxWindowsSurface> DIBTextureStorage::Pin()
{
  MutexAutoLock lock(mMutex);
  CancelLocked();
  mLastUse = TimeStamp::Now();
  if (!mSurface && !RestoreLocked()) return nullptr;
  ++mPins;
  return do_AddRef(mSurface);
}

void DIBTextureStorage::Unpin()
{
  MutexAutoLock lock(mMutex);
  MOZ_ASSERT(mPins);
  --mPins;
  mLastUse = TimeStamp::Now();
}

int32_t DIBTextureStorage::IdleWaitMs(uint32_t aDelayMs)
{
  MutexAutoLock lock(mMutex);
  if (!mSurface || mPermanentlyRaw || mRawBytes < 256 * 1024 ||
      mRawBytes > 64 * 1024 * 1024 || mFormat != SurfaceFormat::B8G8R8X8)
    return -1;
  double remaining = aDelayMs - (TimeStamp::Now() - mLastUse).ToMilliseconds();
  if (mPins) return std::max(int32_t(100), int32_t(ceil(remaining)));
  return std::max(int32_t(0), int32_t(ceil(remaining)));
}

DIBTextureStorage::Result DIBTextureStorage::CompressOneBlock()
{
  MutexAutoLock lock(mMutex);
  if (!mSurface || mPins || mPermanentlyRaw || !mRawBytes ||
      mStride <= 0 || mRawBytes % 4 || mFormat != SurfaceFormat::B8G8R8X8)
    return Result::Stopped;
  if (!mOffset) ++sAttemptCount;
  cairo_surface_t* surface = mSurface->CairoSurface();
  cairo_surface_t* image = cairo_win32_surface_get_image(surface);
  if (!image || cairo_surface_get_reference_count(surface) != 1 ||
      cairo_surface_get_reference_count(image) != 1) {
    CancelLocked();
    mPermanentlyRaw = true;
    ++sRejectedCount;
    ++sAliasRejectedCount;
    return Result::Stopped;
  }
  TimeStamp start = TimeStamp::Now();
  uint32_t raw = uint32_t(std::min(ColdBlockSize, mRawBytes - mOffset));
  uint8_t scratch[ColdBlockSize];
  const uint8_t* input = cairo_image_surface_get_data(image) + mOffset;
  uint32_t encoded = uint32_t(DIBTextureCodec::Encode(input, raw, scratch, sizeof(scratch)));
  if (!encoded || encoded >= raw) {
    encoded = raw;
    memcpy(scratch, input, raw);
  }
  uint32_t header[] = {raw, encoded};
  if (!mEncoded.AppendElements(reinterpret_cast<uint8_t*>(header), sizeof(header), fallible) ||
      !mEncoded.AppendElements(reinterpret_cast<uint8_t*>(scratch), encoded, fallible)) {
    mOffset = 1; // Clear even a first-block partial append.
    CancelLocked();
    ++sRejectedCount;
    ++sAllocationRejectedCount;
    return Result::Stopped;
  }
  mOffset += raw;
  double usec = (TimeStamp::Now() - start).ToMicroseconds();
  sCompressionUsec += uint64_t(ceil(usec));
  mCodecUsec += usec;
  // Refuse the memory trade on a CPU that cannot encode this backing within
  // four milliseconds of elapsed slice time. Gaps between slices do not count;
  // preemption within a slice does, so this can conservatively reject a buffer.
  // This is deliberately conservative: first-use restoration is synchronous.
  if (mEncoded.Length() > mRawBytes * 3 / 4 || mCodecUsec > 4000) {
    if (mCodecUsec > 4000) ++sBudgetRejectedCount;
    else ++sRatioRejectedCount;
    CancelLocked();
    ++sRejectedCount;
    return Result::Stopped;
  }
  if (mOffset != mRawBytes) return Result::More;
  mEncoded.Compact();
  mSurface = nullptr;
  ++sCompressedCount;
  sCompressedRawBytes += mRawBytes;
  return Result::Compressed;
}

bool DIBTextureStorage::RestoreLocked()
{
  MOZ_ASSERT(!mSurface && mOffset == mRawBytes);
  TimeStamp start = TimeStamp::Now();
  RefPtr<gfxWindowsSurface> surface =
    new gfxWindowsSurface(mSize, SurfaceFormatToImageFormat(mFormat));
  if (surface->CairoStatus()) return false;
  surface->SetMemoryRole(CAIRO_WIN32_MEMORY_TEXTURE);
  cairo_surface_t* image = cairo_win32_surface_get_image(surface->CairoSurface());
  if (!image || cairo_image_surface_get_stride(image) != mStride) return false;
  char* output = reinterpret_cast<char*>(cairo_image_surface_get_data(image));
  size_t offset = 0, input = 0;
  while (input < mEncoded.Length()) {
    uint32_t header[2];
    MOZ_RELEASE_ASSERT(mEncoded.Length() - input >= sizeof(header));
    memcpy(header, mEncoded.Elements() + input, sizeof(header));
    input += sizeof(header);
    uint32_t raw = header[0], encoded = header[1];
    MOZ_RELEASE_ASSERT(raw && raw <= ColdBlockSize && raw <= mRawBytes - offset &&
                       encoded && encoded <= mEncoded.Length() - input);
    const char* source = reinterpret_cast<const char*>(mEncoded.Elements() + input);
    if (encoded == raw) {
      memcpy(output + offset, source, raw);
    } else {
      MOZ_RELEASE_ASSERT(DIBTextureCodec::Decode(
        reinterpret_cast<const uint8_t*>(source), encoded,
        reinterpret_cast<uint8_t*>(output + offset), raw));
    }
    offset += raw;
    input += encoded;
  }
  MOZ_RELEASE_ASSERT(offset == mRawBytes);
  cairo_surface_mark_dirty(image);
  mSurface = surface;
  mEncoded.Clear();
  mEncoded.Compact();
  mOffset = 0;
  mPermanentlyRaw = true;
  ++sRestoredCount;
  sRestoreUsec += uint64_t((TimeStamp::Now() - start).ToMicroseconds());
  return true;
}

bool DIBTextureStorage::IsCompressed()
{
  MutexAutoLock lock(mMutex);
  return !mSurface;
}

} // namespace layers
} // namespace mozilla
