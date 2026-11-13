/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
#include "gtest/gtest.h"
#ifdef XP_WIN
#include "mozilla/layers/DIBTextureStorage.h"
#include "mozilla/layers/TextureDIB.h"
#include "DIBTextureCodec.h"
#include "mozilla/layers/BasicCompositor.h"
#include "mozilla/layers/TextureHostBasic.h"
#include "mozilla/Preferences.h"
#include "gfxPlatform.h"
#include "gfxImageSurface.h"
#include "cairo-win32.h"
#include <algorithm>
#include <vector>

using namespace mozilla;
using namespace mozilla::layers;
using namespace mozilla::gfx;

TEST(ColdDIBCodec, RunsLiteralsAndUnalignedBuffers)
{
  // Include lengths around the run threshold and the maximum tag length.
  for (size_t length : {size_t(1), size_t(2), size_t(3), size_t(32767),
                       size_t(32768), size_t(32769), size_t(65537)}) {
    for (unsigned pattern = 0; pattern < 3; ++pattern) {
      std::vector<uint8_t> input(length * 4 + 2, 0x5a);
      std::vector<uint8_t> output(length * 4 + 2, 0x6b);
      std::vector<uint8_t> encoded(length * 4 + 32, 0x7c);
      for (size_t i = 0; i < length; ++i) {
        uint32_t pixel = pattern == 0 ? 0xff79a53cu :
                         pattern == 1 ? uint32_t(i * 0x19375fu) :
                         uint32_t((i / 5) * 0x13759bu);
        memcpy(input.data() + 1 + i * 4, &pixel, 4);
      }
      size_t bytes = length * 4;
      size_t stored = DIBTextureCodec::Encode(input.data() + 1, bytes,
                                            encoded.data() + 1, encoded.size() - 2);
      ASSERT_GT(stored, 0u);
      ASSERT_TRUE(DIBTextureCodec::Decode(encoded.data() + 1, stored,
                                         output.data() + 1, bytes));
      EXPECT_EQ(0, memcmp(input.data() + 1, output.data() + 1, bytes));
      EXPECT_EQ(0x6b, output.front());
      EXPECT_EQ(0x6b, output.back());
      EXPECT_EQ(0x7c, encoded.front());
      EXPECT_EQ(0x7c, encoded.back());
      // Insufficient capacity must fail without touching the following byte.
      std::vector<uint8_t> small(stored, 0x4d);
      EXPECT_EQ(0u, DIBTextureCodec::Encode(input.data() + 1, bytes,
                                          small.data(), stored - 1));
      EXPECT_EQ(0x4d, small.back());
    }
  }
}

TEST(ColdDIBCodec, TruncatedAndMalformedBlocks)
{
  uint8_t input[128];
  for (size_t i = 0; i < sizeof(input); ++i) input[i] = uint8_t(i / 16);
  uint8_t encoded[256], restored[128];
  size_t stored = DIBTextureCodec::Encode(input, sizeof(input), encoded, sizeof(encoded));
  ASSERT_GT(stored, 0u);
  for (size_t length = 0; length < stored; ++length)
    EXPECT_FALSE(DIBTextureCodec::Decode(encoded, length, restored, sizeof(restored)));
  EXPECT_FALSE(DIBTextureCodec::Decode(encoded, stored, restored, sizeof(restored) - 4));
  EXPECT_FALSE(DIBTextureCodec::Decode(encoded, stored, restored, sizeof(restored) - 1));
  encoded[stored] = 0;
  EXPECT_FALSE(DIBTextureCodec::Decode(encoded, stored + 1, restored, sizeof(restored)));
  uint8_t overrun[] = {0xff, 0xff, 0, 0, 0, 0};
  EXPECT_FALSE(DIBTextureCodec::Decode(overrun, sizeof(overrun), restored, sizeof(restored)));
  EXPECT_EQ(0u, DIBTextureCodec::Encode(input, sizeof(input) - 1, encoded, sizeof(encoded)));
  EXPECT_EQ(0u, DIBTextureCodec::Encode(input, 0, encoded, sizeof(encoded)));
}

TEST(ColdDIBCodec, MixedRunsAndCorruptionBounds)
{
  uint32_t seed = 0x5932a17bu;
  auto next = [&]() {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
  };
  for (unsigned trial = 0; trial < 256; ++trial) {
    size_t pixels = 1 + next() % 16385;
    size_t bytes = pixels * 4;
    std::vector<uint8_t> input(bytes), encoded(bytes + 128), output(bytes + 2, 0xc7);
    for (size_t i = 0; i < pixels;) {
      uint32_t value = next();
      size_t count = std::min(size_t(1 + next() % 97), pixels - i);
      for (size_t j = 0; j < count; ++j) {
        uint32_t pixel = trial % 2 ? value : next();
        memcpy(input.data() + (i + j) * 4, &pixel, 4);
      }
      i += count;
    }
    size_t stored = DIBTextureCodec::Encode(input.data(), bytes,
                                          encoded.data(), encoded.size());
    ASSERT_GT(stored, 0u);
    ASSERT_TRUE(DIBTextureCodec::Decode(encoded.data(), stored, output.data() + 1, bytes));
    EXPECT_EQ(0, memcmp(input.data(), output.data() + 1, bytes));
    encoded[next() % stored] ^= uint8_t(1u << (next() % 8));
    // Corruption can form another valid stream; either result must respect
    // the supplied output capacity, including unaligned vector stores.
    DIBTextureCodec::Decode(encoded.data(), stored, output.data() + 1, bytes);
    EXPECT_EQ(0xc7, output.front());
    EXPECT_EQ(0xc7, output.back());
  }
}

static RefPtr<DIBTextureStorage> ColdDIBFixture(bool aRandom = false)
{
  gfxPlatform::GetPlatform();
  RefPtr<gfxWindowsSurface> surface =
    new gfxWindowsSurface(IntSize(512, 512), gfxImageFormat::X8R8G8B8);
  EXPECT_EQ(0, surface->CairoStatus());
  RefPtr<gfxImageSurface> image = surface->GetAsImageSurface();
  uint32_t seed = 0x12345678;
  for (int y = 0; y < image->Height(); ++y) {
    for (int x = 0; x < image->Stride(); ++x) {
      seed ^= seed << 13;
      seed ^= seed >> 17;
      seed ^= seed << 5;
      image->Data()[y * image->Stride() + x] = aRandom ? uint8_t(seed) : uint8_t(y & 31);
    }
  }
  return new DIBTextureStorage(surface);
}

TEST(ColdDIB, ExactRestoreAndNoCycling)
{
  RefPtr<DIBTextureStorage> storage = ColdDIBFixture();
  unsigned blocks = 0;
  DIBTextureStorage::Result result;
  do {
    result = storage->CompressOneBlock();
    ++blocks;
  } while (result == DIBTextureStorage::Result::More && blocks < 100);
  ASSERT_EQ(DIBTextureStorage::Result::Compressed, result);
  EXPECT_EQ(16u, blocks);
  EXPECT_TRUE(storage->IsCompressed());
  RefPtr<gfxWindowsSurface> surface = storage->Pin();
  ASSERT_TRUE(surface);
  EXPECT_FALSE(storage->IsCompressed());
  RefPtr<gfxImageSurface> image = surface->GetAsImageSurface();
  ASSERT_TRUE(image);
  for (int y = 0; y < image->Height(); ++y)
    for (int x = 0; x < image->Stride(); ++x)
      ASSERT_EQ(uint8_t(y & 31), image->Data()[y * image->Stride() + x]);
  image = nullptr;
  surface = nullptr;
  storage->Unpin();
  EXPECT_EQ(-1, storage->IdleWaitMs(1000));
  EXPECT_EQ(DIBTextureStorage::Result::Stopped, storage->CompressOneBlock());
}

TEST(ColdDIB, PinsAliasesAndInterruptedWork)
{
  RefPtr<DIBTextureStorage> storage = ColdDIBFixture();
  RefPtr<gfxWindowsSurface> surface = storage->Pin();
  ASSERT_TRUE(surface);
  EXPECT_EQ(DIBTextureStorage::Result::Stopped, storage->CompressOneBlock());
  EXPECT_GT(storage->IdleWaitMs(1000), 0);
  RefPtr<gfxImageSurface> alias = surface->GetAsImageSurface();
  surface = nullptr;
  storage->Unpin();
  EXPECT_EQ(DIBTextureStorage::Result::Stopped, storage->CompressOneBlock());
  EXPECT_FALSE(storage->IsCompressed());
  ASSERT_TRUE(alias);
  EXPECT_EQ(uint8_t(1), alias->Data()[alias->Stride()]);
  alias = nullptr;

  storage = ColdDIBFixture();
  EXPECT_EQ(DIBTextureStorage::Result::More, storage->CompressOneBlock());
  surface = storage->Pin();
  ASSERT_TRUE(surface);
  EXPECT_FALSE(storage->IsCompressed());
  surface = nullptr;
  storage->Unpin();
  EXPECT_EQ(DIBTextureStorage::Result::Stopped, storage->CompressOneBlock());
  EXPECT_EQ(-1, storage->IdleWaitMs(1000));
}

TEST(ColdDIB, MixedRawBlocksAndPartialLastBlock)
{
  gfxPlatform::GetPlatform();
  RefPtr<gfxWindowsSurface> surface =
    new gfxWindowsSurface(IntSize(513, 513), gfxImageFormat::X8R8G8B8);
  ASSERT_EQ(0, surface->CairoStatus());
  RefPtr<gfxImageSurface> image = surface->GetAsImageSurface();
  size_t bytes = size_t(image->Stride()) * image->Height();
  std::vector<uint8_t> expected(bytes, 0x81);
  uint32_t seed = 0x12345678;
  for (size_t i = 0; i < 64 * 1024; ++i) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    expected[i] = uint8_t(seed);
  }
  memcpy(image->Data(), expected.data(), bytes);
  image = nullptr;
  RefPtr<DIBTextureStorage> storage = new DIBTextureStorage(surface);
  surface = nullptr;
  DIBTextureStorage::Result result;
  unsigned blocks = 0;
  do {
    result = storage->CompressOneBlock();
    ++blocks;
  } while (result == DIBTextureStorage::Result::More && blocks < 100);
  ASSERT_EQ(DIBTextureStorage::Result::Compressed, result);
  EXPECT_EQ(17u, blocks);
  surface = storage->Pin();
  ASSERT_TRUE(surface);
  image = surface->GetAsImageSurface();
  ASSERT_TRUE(image);
  EXPECT_EQ(0, memcmp(image->Data(), expected.data(), bytes));
  image = nullptr;
  surface = nullptr;
  storage->Unpin();
}

TEST(ColdDIB, IncompressibleAndCancellation)
{
  RefPtr<DIBTextureStorage> storage = ColdDIBFixture(true);
  DIBTextureStorage::Result result;
  unsigned blocks = 0;
  do {
    result = storage->CompressOneBlock();
    ++blocks;
  } while (result == DIBTextureStorage::Result::More && blocks < 100);
  EXPECT_EQ(DIBTextureStorage::Result::Stopped, result);
  EXPECT_FALSE(storage->IsCompressed());
  EXPECT_EQ(-1, storage->IdleWaitMs(1000));
  storage = ColdDIBFixture();
  EXPECT_EQ(DIBTextureStorage::Result::More, storage->CompressOneBlock());
  storage->CancelCompression();
  EXPECT_EQ(DIBTextureStorage::Result::Stopped, storage->CompressOneBlock());
  EXPECT_FALSE(storage->IsCompressed());
}

TEST(ColdDIB, DescriptorBindingAndEscapedSource)
{
  RefPtr<DIBTextureStorage> storage = ColdDIBFixture();
  RefPtr<BasicCompositor> compositor = new BasicCompositor(nullptr, nullptr);
  // Match Serialize's transferred reference; the host consumes it once.
  storage->AddRef();
  SurfaceDescriptorDIB descriptor(reinterpret_cast<uintptr_t>(storage.get()), true);
  RefPtr<DIBTextureHost> host = new DIBTextureHost(TextureFlags::DEFAULT, descriptor);
  host->SetCompositor(compositor);
  host->Updated();
  host->DeallocateDeviceData();
  DIBTextureStorage::Result result;
  do { result = storage->CompressOneBlock(); }
  while (result == DIBTextureStorage::Result::More);
  ASSERT_EQ(DIBTextureStorage::Result::Compressed, result);

  // Bind without Lock (the GenEffect path) must restore first.
  CompositableTextureSourceRef bound;
  ASSERT_TRUE(host->BindTextureSource(bound));
  ASSERT_TRUE(bound->AsSourceBasic());
  RefPtr<DataSourceSurface> pixels = bound->AsSourceBasic()->GetSurface(nullptr)->GetDataSurface();
  ASSERT_TRUE(pixels);
  DataSourceSurface::MappedSurface map;
  ASSERT_TRUE(pixels->Map(DataSourceSurface::READ, &map));
  EXPECT_EQ(uint8_t(1), map.mData[map.mStride]);
  pixels->Unmap();
  host->NotifyActorDestroyed();
  host->SetCompositor(nullptr);
  host = nullptr;
  bound = nullptr;
  // The host is gone; an escaped source still pins real backing, not freed bits.
  ASSERT_TRUE(pixels->Map(DataSourceSurface::READ, &map));
  EXPECT_EQ(uint8_t(1), map.mData[map.mStride]);
  pixels->Unmap();
  pixels = nullptr;
  compositor->Destroy();
}
#endif
