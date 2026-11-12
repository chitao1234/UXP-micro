/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
#include "gtest/gtest.h"
#include "mozilla/layers/BasicCompositor.h"
#include "mozilla/Preferences.h"
#include "gfxPlatform.h"
#include "nsITimer.h"
#include "nsComponentManagerUtils.h"
#include "nsThreadUtils.h"
#ifdef XP_WIN
#include "mozilla/widget/WinCompositorWidget.h"
#include "mozilla/widget/PlatformWidgetTypes.h"
#endif

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::layers;

class PresentationTestWidget final : public widget::CompositorWidget {
public:
  PresentationTestWidget()
    : mDeferred(false), mPresentations(0), mReleases(0)
  {
    mScreen = gfxPlatform::GetPlatform()->CreateOffscreenContentDrawTarget(
      IntSize(64, 64), SurfaceFormat::B8G8R8A8);
  }
  LayoutDeviceIntSize GetClientSize() override { return LayoutDeviceIntSize(64, 64); }
  nsIWidget* RealWidget() override { return nullptr; }
  void ObserveVsync(VsyncObserver*) override {}
  already_AddRefed<DrawTarget> StartRemoteDrawing() override { return do_AddRef(mScreen); }
  void EndRemoteDrawing() override { ++mPresentations; }
  bool NeedsToDeferEndRemoteDrawing() override { return mDeferred; }
  bool SupportsIdleBackBufferRelease() const override { return true; }
  size_t ReleaseBackBuffer() override {
    size_t bytes = CompositorWidget::ReleaseBackBuffer();
    if (bytes) ++mReleases;
    return bytes;
  }
  bool HasBuffer() const { return !!mLastBackBuffer; }
  DrawTarget* Buffer() const { return mLastBackBuffer; }
  RefPtr<DrawTarget> mScreen;
  bool mDeferred;
  unsigned mPresentations;
  unsigned mReleases;
};

static void WaitPresentationEvents(uint32_t aMs)
{
  bool done = false;
  nsCOMPtr<nsITimer> timer = do_CreateInstance(NS_TIMER_CONTRACTID);
  ASSERT_TRUE(timer);
  ASSERT_EQ(NS_OK, timer->InitWithFuncCallback(
    [](nsITimer*, void* data) { *static_cast<bool*>(data) = true; },
    &done, aMs, nsITimer::TYPE_ONE_SHOT));
  while (!done) {
    ASSERT_TRUE(NS_ProcessNextEvent(nullptr, true));
  }
}

class AutoPresentationPolicy {
public:
  AutoPresentationPolicy()
  {
    BasicCompositor::InitializeMemoryPolicy();
    mEnabled = Preferences::GetBool("browser.memory.releaseIdleCompositorBuffer", false);
    mDelay = Preferences::GetUint("browser.memory.idleCompositorBufferDelayMs", 1000);
    mHadEnabled = Preferences::HasUserValue("browser.memory.releaseIdleCompositorBuffer");
    mHadDelay = Preferences::HasUserValue("browser.memory.idleCompositorBufferDelayMs");
    Preferences::SetBool("browser.memory.releaseIdleCompositorBuffer", true);
    Preferences::SetUint("browser.memory.idleCompositorBufferDelayMs", 60);
  }
  ~AutoPresentationPolicy()
  {
    if (mHadEnabled) Preferences::SetBool("browser.memory.releaseIdleCompositorBuffer", mEnabled);
    else Preferences::ClearUser("browser.memory.releaseIdleCompositorBuffer");
    if (mHadDelay) Preferences::SetUint("browser.memory.idleCompositorBufferDelayMs", mDelay);
    else Preferences::ClearUser("browser.memory.idleCompositorBufferDelayMs");
  }
private:
  bool mEnabled, mHadEnabled, mHadDelay;
  uint32_t mDelay;
};

TEST(PresentationBuffer, TierDefaultsAndExplicitOverrides)
{
  gfxPlatform::GetPlatform();
  BasicCompositor::InitializeMemoryPolicy();
  const char* policyPref = "browser.memory.releaseIdleCompositorBuffer";
  const char* tierPref = "browser.memoryTier";
  bool hadPolicy = Preferences::HasUserValue(policyPref);
  bool oldPolicy = Preferences::GetBool(policyPref, false);
  bool hadTier = Preferences::HasUserValue(tierPref);
  int32_t oldTier = Preferences::GetInt(tierPref, 3);
  auto checkTier = [&](int32_t tier, uint32_t expected) {
    Preferences::ClearUser(policyPref);
    Preferences::SetInt(tierPref, tier);
    EXPECT_EQ(expected, BasicCompositor::IdleBackBufferReleaseDelayMs())
      << "tier " << tier;
  };
  checkTier(0, 1000);
  checkTier(1, 1000);
  checkTier(2, 0);
  checkTier(3, 0);
  Preferences::SetInt(tierPref, 3);
  Preferences::SetBool(policyPref, true);
  EXPECT_EQ(1000u, BasicCompositor::IdleBackBufferReleaseDelayMs());
  Preferences::SetBool(policyPref, false);
  EXPECT_EQ(0u, BasicCompositor::IdleBackBufferReleaseDelayMs());
  if (hadPolicy) Preferences::SetBool(policyPref, oldPolicy);
  else Preferences::ClearUser(policyPref);
  if (hadTier) Preferences::SetInt(tierPref, oldTier);
  else Preferences::ClearUser(tierPref);
}

static void PresentTestDamage(BasicCompositor* aCompositor, const IntRect& aRect, Color aColor)
{
  aCompositor->BeginFrame(nsIntRegion(aRect), nullptr, IntRect(0, 0, 64, 64), nsIntRegion());
  auto target = static_cast<BasicCompositingRenderTarget*>(aCompositor->GetCurrentRenderTarget());
  ASSERT_TRUE(target && target->mDrawTarget);
  target->mDrawTarget->FillRect(Rect(aRect), ColorPattern(aColor),
                               DrawOptions(1, CompositionOp::OP_SOURCE));
  aCompositor->EndFrame();
}

static uint32_t PresentationPixel(DrawTarget* aTarget, int x, int y)
{
  RefPtr<SourceSurface> snapshot = aTarget->Snapshot();
  RefPtr<DataSourceSurface> data = snapshot->GetDataSurface();
  DataSourceSurface::ScopedMap map(data, DataSourceSurface::READ);
  if (!map.IsMapped()) return 0;
  uint32_t pixel;
  memcpy(&pixel, map.GetData() + y * map.GetStride() + x * 4, 4);
  return pixel;
}

TEST(PresentationBuffer, IdleReleaseAndClippedRecreation)
{
  gfxPlatform::GetPlatform();
  AutoPresentationPolicy policy;
  RefPtr<PresentationTestWidget> widget = new PresentationTestWidget();
  RefPtr<BasicCompositor> compositor = new BasicCompositor(nullptr, widget);
  PresentTestDamage(compositor, IntRect(0, 0, 64, 64), Color(1, 0, 0, 1));
  ASSERT_TRUE(widget->HasBuffer());
  DrawTarget* buffer = widget->Buffer();
  // Hold a compositor frame across the first release deadline. The timer must
  // retry shortly while rendering is active, without restarting the quiet
  // interval. The next update continues to reuse the same scratch.
  compositor->BeginFrame(nsIntRegion(IntRect(8, 12, 16, 20)), nullptr,
                         IntRect(0, 0, 64, 64), nsIntRegion());
  auto activeTarget = static_cast<BasicCompositingRenderTarget*>(compositor->GetCurrentRenderTarget());
  ASSERT_TRUE(activeTarget && activeTarget->mDrawTarget);
  activeTarget->mDrawTarget->FillRect(Rect(8, 12, 16, 20),
    ColorPattern(Color(0, 1, 0, 1)), DrawOptions(1, CompositionOp::OP_SOURCE));
  WaitPresentationEvents(100);
  EXPECT_TRUE(widget->HasBuffer());
  compositor->EndFrame();
  EXPECT_EQ(buffer, widget->Buffer());
  WaitPresentationEvents(100);
  EXPECT_FALSE(widget->HasBuffer());
  EXPECT_EQ(1u, widget->mReleases);
  EXPECT_EQ(0xff00ff00u, PresentationPixel(widget->mScreen, 10, 14));
  EXPECT_EQ(0xffff0000u, PresentationPixel(widget->mScreen, 2, 2));
  PresentTestDamage(compositor, IntRect(40, 30, 8, 10), Color(0, 0, 1, 1));
  EXPECT_EQ(0xff0000ffu, PresentationPixel(widget->mScreen, 42, 32));
  EXPECT_EQ(0xff00ff00u, PresentationPixel(widget->mScreen, 10, 14));
  EXPECT_EQ(0xffff0000u, PresentationPixel(widget->mScreen, 2, 2));
  compositor->Destroy();
  compositor->DetachWidget();
  WaitPresentationEvents(80); // canceled runnable must not touch detached widget
  EXPECT_EQ(1u, widget->mReleases);
}

TEST(PresentationBuffer, DeferredPresentationAndPolicyDisable)
{
  gfxPlatform::GetPlatform();
  AutoPresentationPolicy policy;
  RefPtr<PresentationTestWidget> widget = new PresentationTestWidget();
  RefPtr<BasicCompositor> compositor = new BasicCompositor(nullptr, widget);
  PresentTestDamage(compositor, IntRect(0, 0, 64, 64), Color(1, 0, 0, 1));
  widget->mDeferred = true;
  PresentTestDamage(compositor, IntRect(0, 0, 32, 32), Color(0, 1, 0, 1));
  WaitPresentationEvents(100);
  EXPECT_TRUE(widget->HasBuffer());
  EXPECT_EQ(0u, widget->mReleases);
  EXPECT_EQ(1u, widget->mPresentations);
  widget->mDeferred = false;
  compositor->FinishPendingComposite();
  EXPECT_EQ(2u, widget->mPresentations);
  EXPECT_EQ(0xff00ff00u, PresentationPixel(widget->mScreen, 1, 1));
  Preferences::SetBool("browser.memory.releaseIdleCompositorBuffer", false);
  WaitPresentationEvents(100);
  EXPECT_TRUE(widget->HasBuffer());
  EXPECT_EQ(0u, widget->mReleases);
  Preferences::SetBool("browser.memory.releaseIdleCompositorBuffer", true);
  PresentTestDamage(compositor, IntRect(0, 0, 16, 16), Color(0, 0, 0, 0));
  WaitPresentationEvents(100);
  EXPECT_FALSE(widget->HasBuffer());
  EXPECT_EQ(0u, PresentationPixel(widget->mScreen, 1, 1));
  compositor->Destroy();
  compositor->DetachWidget();
}

#ifdef XP_WIN
class PresentationNativeWidget final : public widget::WinCompositorWidget {
public:
  explicit PresentationNativeWidget(HWND aWindow)
    : WinCompositorWidget(widget::CompositorWidgetInitData(
        reinterpret_cast<uintptr_t>(aWindow), 0, eTransparencyTransparent)) {}
  nsIWidget* RealWidget() override { return nullptr; }
  void ObserveVsync(VsyncObserver*) override {}
};

TEST(PresentationBuffer, WindowsLocksAndTransparentContents)
{
  gfxPlatform::GetPlatform();
  HWND window = ::CreateWindowExW(WS_EX_LAYERED, L"STATIC", L"", WS_POPUP,
    0, 0, 96, 96, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
  ASSERT_TRUE(window);
  RefPtr<PresentationNativeWidget> widget = new PresentationNativeWidget(window);
  RefPtr<DrawTarget> screen = widget->StartRemoteDrawing();
  ASSERT_TRUE(screen);
  screen->FillRect(Rect(0, 0, 96, 96), ColorPattern(Color(1, 0, 0, 0.5f)),
                   DrawOptions(1, CompositionOp::OP_SOURCE));
  RefPtr<DrawTarget> scratch = widget->GetBackBufferDrawTarget(screen,
    LayoutDeviceIntRect(0, 0, 96, 96), LayoutDeviceIntRect());
  ASSERT_TRUE(scratch);
  EXPECT_EQ(0u, widget->ReleaseBackBuffer()); // native drawing/borrowed bits active
  RefPtr<SourceSurface> snapshot = widget->EndBackBufferDrawing();
  widget->EndRemoteDrawing();
  screen = nullptr;
  scratch = nullptr;
  snapshot = nullptr;
  EXPECT_EQ(size_t(96 * 96 * 4), widget->ReleaseBackBuffer());
  EXPECT_EQ(0u, widget->ReleaseBackBuffer());
  {
    MutexAutoLock lock(widget->GetTransparentSurfaceLock());
    RefPtr<gfxASurface> persistent = widget->EnsureTransparentSurface();
    ASSERT_TRUE(persistent);
    RefPtr<DrawTarget> contents = gfxPlatform::CreateDrawTargetForSurface(persistent, IntSize(96, 96));
    ASSERT_TRUE(contents);
    EXPECT_EQ(0x80800000u, PresentationPixel(contents, 4, 4));
  }
  widget->OnDestroyWindow();
  widget = nullptr;
  ::DestroyWindow(window);
}
#endif
