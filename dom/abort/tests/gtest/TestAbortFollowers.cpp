/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "gtest/gtest.h"
#include "mozilla/dom/AbortSignal.h"
#include "../../AbortSignalTimeout.h"

using mozilla::dom::AbortSignal;

class TestFollower final : public AbortSignal::Follower {
public:
  unsigned calls = 0;
  TestFollower* remove = nullptr;
  AbortSignal* reenter = nullptr;
  bool removeSelf = false;
  void Start(AbortSignal* signal) { Follow(signal); }
  void Stop() { Unfollow(); }
  void Aborted() override {
    ++calls;
    if (removeSelf) Stop();
    if (remove) remove->Stop();
    if (reenter) reenter->Abort();
  }
};

TEST(AbortSignalFollowers, SelfRemovalDoesNotSkipNext) {
  RefPtr<AbortSignal> signal = new AbortSignal(false);
  TestFollower first, second, third;
  first.removeSelf = true;
  second.removeSelf = true;
  first.Start(signal); second.Start(signal); third.Start(signal);
  signal->Abort();
  EXPECT_EQ(1u, first.calls); EXPECT_EQ(1u, second.calls); EXPECT_EQ(1u, third.calls);
}

TEST(AbortSignalFollowers, RemovedLaterFollowerDoesNotRun) {
  RefPtr<AbortSignal> signal = new AbortSignal(false);
  TestFollower first, second, third;
  first.remove = &second;
  first.Start(signal); second.Start(signal); third.Start(signal);
  signal->Abort();
  EXPECT_EQ(1u, first.calls); EXPECT_EQ(0u, second.calls); EXPECT_EQ(1u, third.calls);
}

TEST(AbortSignalFollowers, ReentrancyAndFirstAbortWins) {
  RefPtr<AbortSignal> signal = new AbortSignal(false);
  TestFollower first, second;
  first.reenter = signal;
  first.Start(signal); second.Start(signal);
  signal->Abort(); signal->Abort(NS_ERROR_DOM_TIMEOUT_ERR);
  EXPECT_TRUE(signal->Aborted());
  EXPECT_EQ(1u, first.calls); EXPECT_EQ(1u, second.calls);
}

TEST(AbortSignalTimeout, LongDeadlineSlicesDoNotWrapOrAbortEarly) {
  using mozilla::dom::TakeAbortTimeoutSlice;
  uint64_t remaining = UINT64_MAX;
  EXPECT_EQ(INT32_MAX, TakeAbortTimeoutSlice(remaining, INT32_MAX));
  EXPECT_EQ(UINT64_MAX - uint64_t(INT32_MAX), remaining);
  remaining = uint64_t(INT32_MAX) * 2 + 17;
  EXPECT_EQ(INT32_MAX, TakeAbortTimeoutSlice(remaining, INT32_MAX));
  EXPECT_EQ(INT32_MAX, TakeAbortTimeoutSlice(remaining, INT32_MAX));
  EXPECT_EQ(17, TakeAbortTimeoutSlice(remaining, INT32_MAX));
  EXPECT_EQ(uint64_t(0), remaining);
  EXPECT_EQ(0, TakeAbortTimeoutSlice(remaining, INT32_MAX));
  remaining = 100;
  EXPECT_EQ(30, TakeAbortTimeoutSlice(remaining, 30));
  EXPECT_EQ(uint64_t(70), remaining);
}
