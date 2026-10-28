/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/ContentClient.h"
#include "BasicLayers.h"                // for BasicLayerManager
#include "gfxContext.h"                 // for gfxContext, etc
#include "gfx2DGlue.h"
#include "gfxPlatform.h"                // for gfxPlatform
#include "gfxEnv.h"                     // for gfxEnv
#include "gfxPrefs.h"                   // for gfxPrefs
#include "gfxPoint.h"                   // for IntSize, gfxPoint
#include "gfxUtils.h"                   // for gfxUtils
#include "ipc/ShadowLayers.h"           // for ShadowLayerForwarder
#include "mozilla/ArrayUtils.h"         // for ArrayLength
#include "mozilla/Maybe.h"
#include "mozilla/Preferences.h"
#include "mozilla/gfx/2D.h"             // for DrawTarget, Factory
#include "mozilla/gfx/BasePoint.h"      // for BasePoint
#include "mozilla/gfx/BaseSize.h"       // for BaseSize
#include "mozilla/gfx/Rect.h"           // for Rect
#include "mozilla/gfx/Types.h"
#include "mozilla/layers/CompositorBridgeChild.h" // for CompositorBridgeChild
#include "ClientLayerManager.h"
#include "mozilla/layers/LayerManagerComposite.h"
#include "mozilla/layers/LayersMessages.h"  // for ThebesBufferData
#include "mozilla/layers/LayersTypes.h"
#include "nsDebug.h"                    // for NS_ASSERTION, NS_WARNING, etc
#include "nsISupportsImpl.h"            // for gfxContext::Release, etc
#include "nsIWidget.h"                  // for nsIWidget
#include "nsLayoutUtils.h"
#include "nsITimer.h"
#include "nsThreadUtils.h"
#ifdef XP_WIN
#include "gfxWindowsPlatform.h"
#endif
#ifdef MOZ_WIDGET_GTK
#include "gfxPlatformGtk.h"
#endif
#include "ReadbackLayer.h"

#include <vector>

using namespace std;

namespace mozilla {

using namespace gfx;

namespace layers {

static TextureFlags TextureFlagsForRotatedContentBufferFlags(uint32_t aBufferFlags)
{
  TextureFlags result = TextureFlags::NO_FLAGS;

  if (aBufferFlags & RotatedContentBuffer::BUFFER_COMPONENT_ALPHA) {
    result |= TextureFlags::COMPONENT_ALPHA;
  }

  return result;
}

/* static */ already_AddRefed<ContentClient>
ContentClient::CreateContentClient(CompositableForwarder* aForwarder)
{
  LayersBackend backend = aForwarder->GetCompositorBackendType();
  if (backend != LayersBackend::LAYERS_OPENGL &&
      backend != LayersBackend::LAYERS_D3D9 &&
      backend != LayersBackend::LAYERS_D3D11 &&
      backend != LayersBackend::LAYERS_BASIC) {
    return nullptr;
  }

  bool useDoubleBuffering = false;

#ifdef XP_WIN
  if (backend == LayersBackend::LAYERS_D3D11) {
    useDoubleBuffering = gfxWindowsPlatform::GetPlatform()->IsDirect2DBackend();
  } else
#endif
#ifdef MOZ_WIDGET_GTK
  // We can't use double buffering when using image content with
  // Xrender support on Linux, as ContentHostDoubleBuffered is not
  // suited for direct uploads to the server.
  // FIXME: Even though the comment above suggests that double buffering
  //        is supposed to be disabled when Xrender support is being enabled
  //        (and used), it really wasn't. Historically,
  //        UseImageOffscreenSurfaces() was always false in GTK2 builds, thus
  //        triggering the check, regardless of UseXRender().
  //        Some time later, offscreen surfaces were always enabled, but the
  //        Xrender functionality broke due to not using Xlib-based surfaces.
  //        Using Xlib-based surfaces compatible with Xrender operations seems
  //        to lead to weird graphical artifacts (bars and stripes) on some
  //        hardware (Intel-based?) when displaying quickly-changing content,
  //        so contrary to the statement above we'd better enable double
  //        buffering - which also seems to not have any negative performance
  //        impact.
  if (!gfxPlatformGtk::GetPlatform()->UseImageOffscreenSurfaces() ||
       gfxVars::UseXRender())
#endif
  {
    useDoubleBuffering = (LayerManagerComposite::SupportsDirectTexturing() &&
                         backend != LayersBackend::LAYERS_D3D9) ||
                         backend == LayersBackend::LAYERS_BASIC;
  }

  if (useDoubleBuffering || gfxEnv::ForceDoubleBuffering()) {
    return MakeAndAddRef<ContentClientDoubleBuffered>(aForwarder);
  }
  return MakeAndAddRef<ContentClientSingleBuffered>(aForwarder);
}

void
ContentClient::EndPaint(nsTArray<ReadbackProcessor::Update>* aReadbackUpdates)
{
}

void
ContentClient::PrintInfo(std::stringstream& aStream, const char* aPrefix)
{
  aStream << aPrefix;
  aStream << nsPrintfCString("ContentClient (0x%p)", this).get();

  if (profiler_feature_active("displaylistdump")) {
    nsAutoCString pfx(aPrefix);
    pfx += "  ";

    Dump(aStream, pfx.get(), false);
  }
}

// We pass a null pointer for the ContentClient Forwarder argument, which means
// this client will not have a ContentHost on the other side.
ContentClientBasic::ContentClientBasic(gfx::BackendType aBackend)
  : ContentClient(nullptr)
  , RotatedContentBuffer(ContainsVisibleBounds)
  , mBackend(aBackend)
{}

void
ContentClientBasic::CreateBuffer(ContentType aType,
                                 const IntRect& aRect,
                                 uint32_t aFlags,
                                 RefPtr<gfx::DrawTarget>* aBlackDT,
                                 RefPtr<gfx::DrawTarget>* aWhiteDT)
{
  MOZ_ASSERT(!(aFlags & BUFFER_COMPONENT_ALPHA));
  if (aFlags & BUFFER_COMPONENT_ALPHA) {
    gfxDevCrash(LogReason::AlphaWithBasicClient) << "Asking basic content client for component alpha";
  }

  IntSize size(aRect.width, aRect.height);
#ifdef XP_WIN
  if (mBackend == BackendType::CAIRO && 
      (aType == gfxContentType::COLOR || aType == gfxContentType::COLOR_ALPHA)) {
    RefPtr<gfxWindowsSurface> surf =
      new gfxWindowsSurface(size, aType == gfxContentType::COLOR ? gfxImageFormat::X8R8G8B8_UINT32 :
                                                                   gfxImageFormat::A8R8G8B8_UINT32);
    surf->SetMemoryRole(CAIRO_WIN32_MEMORY_CONTENT);
    *aBlackDT = gfxPlatform::GetPlatform()->CreateDrawTargetForSurface(surf, size);

    if (*aBlackDT) {
      return;
    }
  }
#endif

  *aBlackDT = gfxPlatform::GetPlatform()->CreateDrawTargetForBackend(
    mBackend, size,
    gfxPlatform::GetPlatform()->Optimal2DFormatForContent(aType));
}

void
ContentClientRemoteBuffer::DestroyBuffers()
{
  if (!mTextureClient) {
    return;
  }

  mOldTextures.AppendElement(mTextureClient);
  mTextureClient = nullptr;
  if (mTextureClientOnWhite) {
    mOldTextures.AppendElement(mTextureClientOnWhite);
    mTextureClientOnWhite = nullptr;
  }

  DestroyFrontBuffer();
}

class RemoteBufferReadbackProcessor : public TextureReadbackSink
{
public:
  RemoteBufferReadbackProcessor(nsTArray<ReadbackProcessor::Update>* aReadbackUpdates,
                                const IntRect& aBufferRect, const nsIntPoint& aBufferRotation)
    : mReadbackUpdates(*aReadbackUpdates)
    , mBufferRect(aBufferRect)
    , mBufferRotation(aBufferRotation)
  {
    for (uint32_t i = 0; i < mReadbackUpdates.Length(); ++i) {
      mLayerRefs.push_back(mReadbackUpdates[i].mLayer);
    }
  }

  virtual void ProcessReadback(gfx::DataSourceSurface *aSourceSurface)
  {
    SourceRotatedBuffer rotBuffer(aSourceSurface, nullptr, mBufferRect, mBufferRotation);

    for (uint32_t i = 0; i < mReadbackUpdates.Length(); ++i) {
      ReadbackProcessor::Update& update = mReadbackUpdates[i];
      nsIntPoint offset = update.mLayer->GetBackgroundLayerOffset();

      ReadbackSink* sink = update.mLayer->GetSink();

      if (!sink) {
        continue;
      }

      if (!aSourceSurface) {
        sink->SetUnknown(update.mSequenceCounter);
        continue;
      }

      RefPtr<DrawTarget> dt =
        sink->BeginUpdate(update.mUpdateRect + offset, update.mSequenceCounter);
      if (!dt) {
        continue;
      }

      dt->SetTransform(Matrix::Translation(offset.x, offset.y));

      rotBuffer.DrawBufferWithRotation(dt, RotatedBuffer::BUFFER_BLACK);

      update.mLayer->GetSink()->EndUpdate(update.mUpdateRect + offset);
    }
  }

private:
  nsTArray<ReadbackProcessor::Update> mReadbackUpdates;
  // This array is used to keep the layers alive until the callback.
  vector<RefPtr<Layer>> mLayerRefs;

  IntRect mBufferRect;
  nsIntPoint mBufferRotation;
};

void
ContentClientRemoteBuffer::BeginPaint()
{
  EnsureBackBufferIfFrontBuffer();

  // XXX: So we might not have a TextureClient yet.. because it will
  // only be created by CreateBuffer.. which will deliver a locked surface!.
  if (mTextureClient) {
    SetBufferProvider(mTextureClient);
  }
  if (mTextureClientOnWhite) {
    SetBufferProviderOnWhite(mTextureClientOnWhite);
  }
}

void
ContentClientRemoteBuffer::EndPaint(nsTArray<ReadbackProcessor::Update>* aReadbackUpdates)
{
  MOZ_ASSERT(!mTextureClientOnWhite || !aReadbackUpdates || aReadbackUpdates->Length() == 0);

  // XXX: We might still not have a texture client if PaintThebes
  // decided we didn't need one yet because the region to draw was empty.
  SetBufferProvider(nullptr);
  SetBufferProviderOnWhite(nullptr);
  for (unsigned i = 0; i< mOldTextures.Length(); ++i) {
    if (mOldTextures[i]->IsLocked()) {
      mOldTextures[i]->Unlock();
    }
  }
  mOldTextures.Clear();

  if (mTextureClient && mTextureClient->IsLocked()) {
    if (aReadbackUpdates->Length() > 0) {
      RefPtr<TextureReadbackSink> readbackSink = new RemoteBufferReadbackProcessor(aReadbackUpdates, mBufferRect, mBufferRotation);

      mTextureClient->SetReadbackSink(readbackSink);
    }

    mTextureClient->Unlock();
    mTextureClient->SyncWithObject(mForwarder->GetSyncObject());
  }
  if (mTextureClientOnWhite && mTextureClientOnWhite->IsLocked()) {
    mTextureClientOnWhite->Unlock();
    mTextureClientOnWhite->SyncWithObject(mForwarder->GetSyncObject());
  }

  ContentClientRemote::EndPaint(aReadbackUpdates);
}

void
ContentClientRemoteBuffer::BuildTextureClients(SurfaceFormat aFormat,
                                               const IntRect& aRect,
                                               uint32_t aFlags)
{
  // If we hit this assertion, then it might be due to an empty transaction
  // followed by a real transaction. Our buffers should be created (but not
  // painted in the empty transaction) and then painted (but not created) in the
  // real transaction. That is kind of fragile, and this assert will catch
  // circumstances where we screw that up, e.g., by unnecessarily recreating our
  // buffers.
  MOZ_ASSERT(!mIsNewBuffer,
             "Bad! Did we create a buffer twice without painting?");

  mIsNewBuffer = true;

  DestroyBuffers();

  mSurfaceFormat = aFormat;
  mSize = IntSize(aRect.width, aRect.height);
  mTextureFlags = TextureFlagsForRotatedContentBufferFlags(aFlags);

  if (aFlags & BUFFER_COMPONENT_ALPHA) {
    mTextureFlags |= TextureFlags::COMPONENT_ALPHA;
  }

  CreateBackBuffer(mBufferRect);
}

void
ContentClientRemoteBuffer::CreateBackBuffer(const IntRect& aBufferRect,
                                            bool aPreserveFrontOnFailure)
{
  // gfx::BackendType::NONE means fallback to the content backend
  TextureAllocationFlags textureAllocFlags
                         = (mTextureFlags & TextureFlags::COMPONENT_ALPHA) ?
                            TextureAllocationFlags::ALLOC_CLEAR_BUFFER_BLACK :
                            TextureAllocationFlags::ALLOC_CLEAR_BUFFER;

  mTextureClient = CreateTextureClientForDrawing(
    mSurfaceFormat, mSize, BackendSelector::Content,
    mTextureFlags | ExtraTextureFlags(),
    textureAllocFlags
  );
  if (!mTextureClient || !AddTextureClient(mTextureClient)) {
    if (aPreserveFrontOnFailure) {
      ContentClientRemoteBuffer::AbortTextureClientCreation();
    } else {
      AbortTextureClientCreation();
    }
    return;
  }

  if (mTextureFlags & TextureFlags::COMPONENT_ALPHA) {
    mTextureClientOnWhite = mTextureClient->CreateSimilar(
      mForwarder->GetCompositorBackendType(),
      mTextureFlags | ExtraTextureFlags(),
      TextureAllocationFlags::ALLOC_CLEAR_BUFFER_WHITE
    );
    if (!mTextureClientOnWhite || !AddTextureClient(mTextureClientOnWhite)) {
      if (aPreserveFrontOnFailure) {
        ContentClientRemoteBuffer::AbortTextureClientCreation();
      } else {
        AbortTextureClientCreation();
      }
      return;
    }
  }
}

void
ContentClientRemoteBuffer::CreateBuffer(ContentType aType,
                                        const IntRect& aRect,
                                        uint32_t aFlags,
                                        RefPtr<gfx::DrawTarget>* aBlackDT,
                                        RefPtr<gfx::DrawTarget>* aWhiteDT)
{
  BuildTextureClients(gfxPlatform::GetPlatform()->Optimal2DFormatForContent(aType), aRect, aFlags);
  if (!mTextureClient) {
    return;
  }

  // We just created the textures and we are about to get their draw targets
  // so we have to lock them here.
  DebugOnly<bool> locked = mTextureClient->Lock(OpenMode::OPEN_READ_WRITE);
  MOZ_ASSERT(locked, "Could not lock the TextureClient");

  *aBlackDT = mTextureClient->BorrowDrawTarget();
  if (aFlags & BUFFER_COMPONENT_ALPHA) {
    locked = mTextureClientOnWhite->Lock(OpenMode::OPEN_READ_WRITE);
    MOZ_ASSERT(locked, "Could not lock the second TextureClient for component alpha");

    *aWhiteDT = mTextureClientOnWhite->BorrowDrawTarget();
  }
}

nsIntRegion
ContentClientRemoteBuffer::GetUpdatedRegion(const nsIntRegion& aRegionToDraw,
                                            const nsIntRegion& aVisibleRegion,
                                            bool aDidSelfCopy)
{
  nsIntRegion updatedRegion;
  if (mIsNewBuffer || aDidSelfCopy) {
    // A buffer reallocation clears both buffers. The front buffer has all the
    // content by now, but the back buffer is still clear. Here, in effect, we
    // are saying to copy all of the pixels of the front buffer to the back.
    // Also when we self-copied in the buffer, the buffer space
    // changes and some changed buffer content isn't reflected in the
    // draw or invalidate region (on purpose!).  When this happens, we
    // need to read back the entire buffer too.
    updatedRegion = aVisibleRegion.GetBounds();
    mIsNewBuffer = false;
  } else {
    updatedRegion = aRegionToDraw;
  }

  NS_ASSERTION(BufferRect().Contains(aRegionToDraw.GetBounds()),
               "Update outside of buffer rect!");
  MOZ_ASSERT(mTextureClient, "should have a back buffer by now");

  return updatedRegion;
}

void
ContentClientRemoteBuffer::Updated(const nsIntRegion& aRegionToDraw,
                                   const nsIntRegion& aVisibleRegion,
                                   bool aDidSelfCopy)
{
  nsIntRegion updatedRegion = GetUpdatedRegion(aRegionToDraw,
                                               aVisibleRegion,
                                               aDidSelfCopy);

  MOZ_ASSERT(mTextureClient);
  if (mTextureClientOnWhite) {
    mForwarder->UseComponentAlphaTextures(this, mTextureClient,
                                          mTextureClientOnWhite);
  } else {
    AutoTArray<CompositableForwarder::TimedTextureClient,1> textures;
    CompositableForwarder::TimedTextureClient* t = textures.AppendElement();
    t->mTextureClient = mTextureClient;
    IntSize size = mTextureClient->GetSize();
    t->mPictureRect = nsIntRect(0, 0, size.width, size.height);
    GetForwarder()->UseTextures(this, textures);
  }
  mForwarder->UpdateTextureRegion(this,
                                  ThebesBufferData(BufferRect(),
                                                   BufferRotation()),
                                  updatedRegion);
}

void
ContentClientRemoteBuffer::SwapBuffers(const nsIntRegion& aFrontUpdatedRegion)
{
  mFrontAndBackBufferDiffer = true;
}

void
ContentClientRemoteBuffer::Dump(std::stringstream& aStream,
                                const char* aPrefix,
                                bool aDumpHtml, TextureDumpMode aCompress)
{
  // TODO We should combine the OnWhite/OnBlack here an just output a single image.
  if (!aDumpHtml) {
    aStream << "\n" << aPrefix << "Surface: ";
  }
  CompositableClient::DumpTextureClient(aStream, mTextureClient, aCompress);
}

void
ContentClientDoubleBuffered::Dump(std::stringstream& aStream,
                                  const char* aPrefix,
                                  bool aDumpHtml, TextureDumpMode aCompress)
{
  // TODO We should combine the OnWhite/OnBlack here an just output a single image.
  if (!aDumpHtml) {
    aStream << "\n" << aPrefix << "Surface: ";
  }
  CompositableClient::DumpTextureClient(aStream, mFrontClient, aCompress);
}

void
ContentClientDoubleBuffered::DestroyFrontBuffer()
{
  if (mFrontClient) {
    mOldTextures.AppendElement(mFrontClient);
    mFrontClient = nullptr;
  }

  if (mFrontClientOnWhite) {
    mOldTextures.AppendElement(mFrontClientOnWhite);
    mFrontClientOnWhite = nullptr;
  }
}

ContentClientDoubleBuffered::~ContentClientDoubleBuffered()
{
  CancelIdleBackBufferTimer();
}

uint32_t
ContentClientDoubleBuffered::IdleBackBufferDelay() const
{
  // Basic compositing shares these heap buffers directly. Preserve the GPU
  // buffering and synchronization policy on hardware compositors.
  if (mForwarder->GetCompositorBackendType() != LayersBackend::LAYERS_BASIC) {
    return 0;
  }
  int32_t tier = Preferences::GetInt("browser.memoryTier", 3);
  if (!Preferences::GetBool("browser.memory.releaseIdleBackBuffers",
                            tier >= 0 && tier <= 1)) {
    return 0;
  }
  return Preferences::GetUint("browser.memory.idleBackBufferDelayMs", 5000);
}

void
ContentClientDoubleBuffered::CancelIdleBackBufferTimer()
{
  MOZ_ASSERT(NS_IsMainThread());
  if (mIdleBackBufferTimer) {
    mIdleBackBufferTimer->Cancel();
  }
}

void
ContentClientDoubleBuffered::ArmIdleBackBufferTimer()
{
  MOZ_ASSERT(NS_IsMainThread());
  uint32_t delay = IdleBackBufferDelay();
  if (!delay || mUsedForReadback || mWaitingForBufferSwap ||
      !mTextureClient || !mFrontClient) {
    return;
  }
  if (!mIdleBackBufferTimer) {
    mIdleBackBufferTimer = do_CreateInstance("@mozilla.org/timer;1");
  }
  if (mIdleBackBufferTimer) {
    // Clean layers are visited in transactions triggered by unrelated layers.
    // Their visits must not extend the lifetime of an unused spare buffer.
    if (!mLastBufferUpdate.IsNull()) {
      double elapsed = (TimeStamp::Now() - mLastBufferUpdate).ToMilliseconds();
      delay = elapsed >= delay ? 1 : uint32_t(delay - elapsed);
    }
    mIdleBackBufferTimer->InitWithFuncCallback(IdleBackBufferTimerCallback, this,
                                              delay, nsITimer::TYPE_ONE_SHOT);
  }
}

/* static */ void
ContentClientDoubleBuffered::IdleBackBufferTimerCallback(nsITimer* aTimer,
                                                        void* aClosure)
{
  static_cast<ContentClientDoubleBuffered*>(aClosure)->DiscardIdleBackBuffer();
}

void
ContentClientDoubleBuffered::DiscardIdleBackBuffer()
{
  MOZ_ASSERT(NS_IsMainThread());
  if (!IdleBackBufferDelay() || mUsedForReadback ||
      mWaitingForBufferSwap || !mFrontClient ||
      !mTextureClient || mBufferProvider || mBufferProviderOnWhite) {
    return;
  }
  if (mTextureClient->IsLocked() || mTextureClient->IsReadLocked() ||
      (mTextureClientOnWhite &&
       (mTextureClientOnWhite->IsLocked() || mTextureClientOnWhite->IsReadLocked()))) {
    // Avoid polling a temporarily held compositor lock every millisecond once
    // the original idle deadline has passed.
    mIdleBackBufferTimer->InitWithFuncCallback(IdleBackBufferTimerCallback, this,
                                              16, nsITimer::TYPE_ONE_SHOT);
    return;
  }

  // Release the spare through the normal texture/IPC lifetime. The compositor
  // keeps displaying mFrontClient. Recreate and copy all surviving front pixels
  // on the next paint, rather than just the most recent update region.
  mTextureClient = nullptr;
  mTextureClientOnWhite = nullptr;
  mBackBufferDiscarded = true;
  mFrontUpdatedRegion = mFrontBufferRect;
  mFrontAndBackBufferDiffer = true;
  mDidSelfCopy = false;
}

void
ContentClientDoubleBuffered::EndPaint(nsTArray<ReadbackProcessor::Update>* aReadbackUpdates)
{
  if (aReadbackUpdates && !aReadbackUpdates->IsEmpty()) {
    // Readback sinks require the original EndPaint path with a writable buffer.
    // Exclude these layers from idle release for their remaining lifetime.
    mUsedForReadback = true;
    CancelIdleBackBufferTimer();
    if (mBackBufferDiscarded && mFrontClient) {
      // A layer can acquire its first readback sink after becoming idle.
      // Restore the entire buffer before the normal readback handling below.
      ContentClientRemoteBuffer::BeginPaint();
      if (mTextureClient) {
        FinalizeFrame(nsIntRegion());
      }
    }
  }
  ContentClientRemoteBuffer::EndPaint(aReadbackUpdates);
  // A no-op transaction may recreate a spare without swapping. It still needs
  // to expire when the layer becomes idle again.
  ArmIdleBackBufferTimer();
}

void
ContentClientDoubleBuffered::Updated(const nsIntRegion& aRegionToDraw,
                                     const nsIntRegion& aVisibleRegion,
                                     bool aDidSelfCopy)
{
  mLastBufferUpdate = TimeStamp::Now();
  mWaitingForBufferSwap = true;
  ContentClientRemoteBuffer::Updated(aRegionToDraw, aVisibleRegion, aDidSelfCopy);
}

void
ContentClientDoubleBuffered::SwapBuffers(const nsIntRegion& aFrontUpdatedRegion)
{
  mFrontUpdatedRegion = aFrontUpdatedRegion;

  RefPtr<TextureClient> oldBack = mTextureClient;
  mTextureClient = mFrontClient;
  mFrontClient = oldBack;

  oldBack = mTextureClientOnWhite;
  mTextureClientOnWhite = mFrontClientOnWhite;
  mFrontClientOnWhite = oldBack;

  IntRect oldBufferRect = mBufferRect;
  mBufferRect = mFrontBufferRect;
  mFrontBufferRect = oldBufferRect;

  nsIntPoint oldBufferRotation = mBufferRotation;
  mBufferRotation = mFrontBufferRotation;
  mFrontBufferRotation = oldBufferRotation;

  MOZ_ASSERT(mFrontClient);

  ContentClientRemoteBuffer::SwapBuffers(aFrontUpdatedRegion);
  mWaitingForBufferSwap = false;
  ArmIdleBackBufferTimer();
}

void
ContentClientDoubleBuffered::BeginPaint()
{
  CancelIdleBackBufferTimer();
  if (!mBackBufferDiscarded) {
    ContentClientRemoteBuffer::BeginPaint();
  }

  mIsNewBuffer = false;

  if (!mFrontAndBackBufferDiffer) {
    return;
  }

  if (mDidSelfCopy) {
    // We can't easily draw our front buffer into us, since we're going to be
    // copying stuff around anyway it's easiest if we just move our situation
    // to non-rotated while we're at it. If this situation occurs we'll have
    // hit a self-copy path in PaintThebes before as well anyway.
    mBufferRect.MoveTo(mFrontBufferRect.TopLeft());
    mBufferRotation = nsIntPoint();
    return;
  }
  mBufferRect = mFrontBufferRect;
  mBufferRotation = mFrontBufferRotation;
}

ContentClientRemoteBuffer::PaintState
ContentClientDoubleBuffered::BeginPaintBuffer(PaintedLayer* aLayer, uint32_t aFlags)
{
  PaintState shrunk;
  if (TryShrinkBuffer(aLayer, aFlags, shrunk)) {
    return shrunk;
  }
  if (mBackBufferDiscarded && mFrontClient) {
    nsIntRegion visible = aLayer->GetVisibleRegion().ToUnknownRegion();
    if (visible.IsEmpty()) {
      return PaintState();
    }
    ContentType contentType = aLayer->CanUseOpaqueSurface()
                            ? gfxContentType::COLOR : gfxContentType::COLOR_ALPHA;
    SurfaceMode mode = aLayer->GetSurfaceMode();
    if (mode == SurfaceMode::SURFACE_COMPONENT_ALPHA) {
      if (!aLayer->GetParent() ||
          !aLayer->GetParent()->SupportsComponentAlphaChildren() ||
          !aLayer->AsShadowableLayer() ||
          !aLayer->AsShadowableLayer()->HasShadow()) {
        mode = SurfaceMode::SURFACE_SINGLE_CHANNEL_ALPHA;
      } else {
        contentType = gfxContentType::COLOR;
      }
    }
    nsIntRegion needed = visible;
    if ((aFlags & PAINT_WILL_RESAMPLE) &&
        (!visible.GetBounds().IsEqualInterior(mFrontBufferRect) ||
         visible.GetNumRects() > 1)) {
      if (mode == SurfaceMode::SURFACE_OPAQUE) {
        contentType = gfxContentType::COLOR_ALPHA;
      }
      needed = mFrontBufferRect;
    }
    // Match RotatedContentBuffer's effective format and resampling rules. Raw
    // surface mode alone is insufficient (e.g. component alpha on Basic).
    if (aLayer->GetValidRegion().Contains(needed) &&
        mFrontBufferRect.Contains(visible.GetBounds()) &&
        contentType == ContentForFormat(mFrontClient->GetFormat()) &&
        (mode == SurfaceMode::SURFACE_COMPONENT_ALPHA) == !!mFrontClientOnWhite) {
      return PaintState();
    }
    ContentClientRemoteBuffer::BeginPaint();
  }

  // Allocation failure must leave the authoritative front visible and preserve
  // the layer's dirty region for a later retry, rather than partially painting
  // a fresh, otherwise empty buffer.
  if (!mTextureClient && mFrontClient) {
    return PaintState();
  }
  return ContentClientRemoteBuffer::BeginPaintBuffer(aLayer, aFlags);
}

bool
ContentClientDoubleBuffered::TryShrinkBuffer(PaintedLayer* aLayer,
                                             uint32_t aFlags,
                                             PaintState& aPaintState)
{
  // Repaint substantially oversized software buffers during a normal layer
  // transaction. Never modify the displayed texture or copy its rotated pixels.
  // GPU/component-alpha/readback lifetimes keep their existing policy.
  int32_t tier = Preferences::GetInt("browser.memoryTier", 3);
  if (mForwarder->GetCompositorBackendType() != LayersBackend::LAYERS_BASIC ||
      !Preferences::GetBool("browser.memory.shrinkPaintBuffers",
                            tier >= 0 && tier <= 1) ||
      !mFrontClient || mFrontClientOnWhite || mTextureClientOnWhite ||
      mWaitingForBufferSwap || mUsedForReadback || aLayer->UsedForReadback() ||
      mLoanedDrawTarget) {
    return false;
  }
  ClientLayerManager* manager = aLayer->Manager()->AsClientLayerManager();
  if (!manager || !manager->GetPaintedLayerCallback()) {
    return false; // Empty transactions cannot supply the required full repaint.
  }

  nsIntRegion visible = aLayer->GetVisibleRegion().ToUnknownRegion();
  IntRect rect = visible.GetBounds();
  if (rect.IsEmpty()) {
    return false;
  }
  // Match ComputeBufferRect's minimum width (platform/SIMD requirement).
  rect.width = std::max(rect.width, 64);
  IntSize oldSize = mFrontClient->GetSize();
  int64_t oldPixels = int64_t(oldSize.width) * oldSize.height;
  int64_t newPixels = int64_t(rect.width) * rect.height;
  int64_t savedBytes =
    (oldPixels - newPixels) * BytesPerPixel(mFrontClient->GetFormat());
  int64_t minimum = int64_t(Preferences::GetUint(
    "browser.memory.shrinkPaintBuffers.minSavingsKB", 2048)) * 1024;
  // At least half the pixels and, by default, 2 MiB must be redundant. Small
  // size changes keep reusing their buffers; rapid shrink/grow cycles cannot
  // allocate more than once a second through this path.
  if (newPixels * 2 > oldPixels ||
      savedBytes < minimum ||
      (!mLastBufferShrink.IsNull() &&
       (TimeStamp::Now() - mLastBufferShrink).ToMilliseconds() < 1000)) {
    return false;
  }

  SurfaceMode mode = aLayer->GetSurfaceMode();
  if (mode == SurfaceMode::SURFACE_COMPONENT_ALPHA) {
    if (aLayer->GetParent() &&
        aLayer->GetParent()->SupportsComponentAlphaChildren() &&
        aLayer->AsShadowableLayer() && aLayer->AsShadowableLayer()->HasShadow()) {
      return false;
    }
    mode = SurfaceMode::SURFACE_SINGLE_CHANNEL_ALPHA;
  }
  ContentType content = aLayer->CanUseOpaqueSurface()
                        ? gfxContentType::COLOR : gfxContentType::COLOR_ALPHA;
  nsIntRegion needed = visible;
  if ((aFlags & PAINT_WILL_RESAMPLE) &&
      (!rect.IsEqualInterior(visible.GetBounds()) || visible.GetNumRects() > 1)) {
    if (mode == SurfaceMode::SURFACE_OPAQUE) {
      content = gfxContentType::COLOR_ALPHA;
      mode = SurfaceMode::SURFACE_SINGLE_CHANNEL_ALPHA;
    }
    needed = rect;
  }

  // Stage and validate everything before releasing any current client state.
  // Allocation/registration/locking/draw-target failures simply keep the old
  // buffer and continue through the established painting path below.
  SurfaceFormat format = gfxPlatform::GetPlatform()->Optimal2DFormatForContent(content);
  RefPtr<TextureClient> replacement = CreateTextureClientForDrawing(
    format, rect.Size(), BackendSelector::Content, ExtraTextureFlags(),
    TextureAllocationFlags::ALLOC_CLEAR_BUFFER);
  if (!replacement || !AddTextureClient(replacement) ||
      !replacement->Lock(OpenMode::OPEN_READ_WRITE)) {
    return false;
  }
  RefPtr<DrawTarget> target = replacement->BorrowDrawTarget();
  if (!target || !target->IsValid()) {
    target = nullptr;
    replacement->Unlock();
    return false;
  }

  CancelIdleBackBufferTimer();
  SetBufferProvider(nullptr);
  SetBufferProviderOnWhite(nullptr);
  if (mTextureClient) {
    mOldTextures.AppendElement(mTextureClient);
  }
  // Normal IPC ownership keeps the old front displayed until Updated submits
  // the complete replacement. Do not reuse it as a differently sized spare.
  DestroyFrontBuffer();
  mTextureClient = replacement;
  mTextureFlags = TextureFlags::NO_FLAGS;
  mSurfaceFormat = format;
  mSize = rect.Size();
  SetBufferProvider(mTextureClient);
  mDTBuffer = target;
  mBufferRect = rect;
  mBufferRotation = IntPoint();
  mFrontBufferRect.SetEmpty();
  mFrontBufferRotation = IntPoint();
  mFrontUpdatedRegion.SetEmpty();
  mFrontAndBackBufferDiffer = false;
  mBackBufferDiscarded = false;
  mDidSelfCopy = false;
  mIsNewBuffer = true;
  mLastBufferShrink = TimeStamp::Now();

  aPaintState.mRegionToInvalidate = aLayer->GetValidRegion();
  aPaintState.mRegionToDraw = needed;
  aPaintState.mContentType = content;
  aPaintState.mMode = mode;
  aPaintState.mClip = DrawRegionClip::DRAW;
  return true;
}

// Sync front/back buffers content
// After executing, the new back buffer has the same (interesting) pixels as
// the new front buffer, and mValidRegion et al. are correct wrt the new
// back buffer (i.e. as they were for the old back buffer)
void
ContentClientDoubleBuffered::FinalizeFrame(const nsIntRegion& aRegionToDraw)
{
  if (mTextureClient) {
    DebugOnly<bool> locked = mTextureClient->Lock(OpenMode::OPEN_READ_WRITE);
    MOZ_ASSERT(locked);
  }
  if (mTextureClientOnWhite) {
    DebugOnly<bool> locked = mTextureClientOnWhite->Lock(OpenMode::OPEN_READ_WRITE);
    MOZ_ASSERT(locked);
  }

  if (!mFrontAndBackBufferDiffer) {
    MOZ_ASSERT(!mDidSelfCopy, "If we have to copy the world, then our buffers are different, right?");
    return;
  }
  MOZ_ASSERT(mFrontClient);
  if (!mFrontClient) {
    return;
  }

  MOZ_LAYERS_LOG(("BasicShadowableThebes(%p): reading back <x=%d,y=%d,w=%d,h=%d>",
                  this,
                  mFrontUpdatedRegion.GetBounds().x,
                  mFrontUpdatedRegion.GetBounds().y,
                  mFrontUpdatedRegion.GetBounds().width,
                  mFrontUpdatedRegion.GetBounds().height));

  mFrontAndBackBufferDiffer = false;

  nsIntRegion updateRegion = mFrontUpdatedRegion;
  if (mDidSelfCopy) {
    mDidSelfCopy = false;
    updateRegion = mBufferRect;
  }

  // No point in sync'ing what we are going to draw over anyway. And if there is
  // nothing to sync at all, there is nothing to do and we can go home early.
  updateRegion.Sub(updateRegion, aRegionToDraw);
  if (updateRegion.IsEmpty()) {
    return;
  }

  // We need to ensure that we lock these two buffers in the same
  // order as the compositor to prevent deadlocks.
  TextureClientAutoLock frontLock(mFrontClient, OpenMode::OPEN_READ_ONLY);
  if (!frontLock.Succeeded()) {
    return;
  }
  Maybe<TextureClientAutoLock> frontOnWhiteLock;
  if (mFrontClientOnWhite) {
    frontOnWhiteLock.emplace(mFrontClientOnWhite, OpenMode::OPEN_READ_ONLY);
    if (!frontOnWhiteLock->Succeeded()) {
      return;
    }
  }

  // Restrict the DrawTargets and frontBuffer to a scope to make
  // sure there is no more external references to the DrawTargets
  // when we Unlock the TextureClients.
  gfx::DrawTarget* dt = mFrontClient->BorrowDrawTarget();
  gfx::DrawTarget* dtw = mFrontClientOnWhite ? mFrontClientOnWhite->BorrowDrawTarget() : nullptr;
  if (dt && dt->IsValid()) {
    RefPtr<SourceSurface> surf = dt->Snapshot();
    RefPtr<SourceSurface> surfOnWhite = dtw ? dtw->Snapshot() : nullptr;
    SourceRotatedBuffer frontBuffer(surf,
                                    surfOnWhite,
                                    mFrontBufferRect,
                                    mFrontBufferRotation);
    UpdateDestinationFrom(frontBuffer, updateRegion);
  } else {
    // We know this can happen, but we want to track it somewhat, in case it leads
    // to other problems.
    gfxCriticalNote << "Invalid draw target(s) " << hexa(dt) << " and " << hexa(dtw);
  }
}

void
ContentClientDoubleBuffered::EnsureBackBufferIfFrontBuffer()
{
  if (!mTextureClient && mFrontClient) {
    CreateBackBuffer(mFrontBufferRect, true);
    if (mTextureClient) {
      mBackBufferDiscarded = false;
    }
    // A new spare has no valid pixels, even if the previous front update was
    // tiny. FinalizeFrame will exclude only what is about to be repainted.
    mFrontUpdatedRegion = mFrontBufferRect;
    mFrontAndBackBufferDiffer = true;
    mDidSelfCopy = false;

    mBufferRect = mFrontBufferRect;
    mBufferRotation = mFrontBufferRotation;
  }
}

void
ContentClientDoubleBuffered::UpdateDestinationFrom(const RotatedBuffer& aSource,
                                                   const nsIntRegion& aUpdateRegion)
{
  DrawIterator iter;
  while (DrawTarget* destDT =
    BorrowDrawTargetForQuadrantUpdate(aUpdateRegion.GetBounds(), BUFFER_BLACK, &iter)) {
    bool isClippingCheap = IsClippingCheap(destDT, iter.mDrawRegion);
    if (isClippingCheap) {
      gfxUtils::ClipToRegion(destDT, iter.mDrawRegion);
    }

    aSource.DrawBufferWithRotation(destDT, BUFFER_BLACK, 1.0, CompositionOp::OP_SOURCE);
    if (isClippingCheap) {
      destDT->PopClip();
    }
    // Flush the destination before the sources become inaccessible (Unlock).
    destDT->Flush();
    ReturnDrawTargetToBuffer(destDT);
  }

  if (aSource.HaveBufferOnWhite()) {
    MOZ_ASSERT(HaveBufferOnWhite());
    DrawIterator whiteIter;
    while (DrawTarget* destDT =
      BorrowDrawTargetForQuadrantUpdate(aUpdateRegion.GetBounds(), BUFFER_WHITE, &whiteIter)) {
      bool isClippingCheap = IsClippingCheap(destDT, whiteIter.mDrawRegion);
      if (isClippingCheap) {
        gfxUtils::ClipToRegion(destDT, whiteIter.mDrawRegion);
      }

      aSource.DrawBufferWithRotation(destDT, BUFFER_WHITE, 1.0, CompositionOp::OP_SOURCE);
      if (isClippingCheap) {
        destDT->PopClip();
      }
      // Flush the destination before the sources become inaccessible (Unlock).
      destDT->Flush();
      ReturnDrawTargetToBuffer(destDT);
    }
  }
}

void
ContentClientSingleBuffered::FinalizeFrame(const nsIntRegion& aRegionToDraw)
{
  if (mTextureClient) {
    DebugOnly<bool> locked = mTextureClient->Lock(OpenMode::OPEN_READ_WRITE);
    MOZ_ASSERT(locked);
  }
  if (mTextureClientOnWhite) {
    DebugOnly<bool> locked = mTextureClientOnWhite->Lock(OpenMode::OPEN_READ_WRITE);
    MOZ_ASSERT(locked);
  }
}

} // namespace layers
} // namespace mozilla
