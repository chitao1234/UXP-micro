/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AbortSignal.h"
#include "AbortSignalTimeout.h"

#include "mozilla/dom/Event.h"
#include "mozilla/dom/AbortSignalBinding.h"
#include "mozilla/dom/DOMException.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/TimeoutHandler.h"
#include "mozilla/dom/ToJSValue.h"
#include "mozilla/HoldDropJSObjects.h"
#include "nsGlobalWindow.h"
#include "nsITimer.h"
#include "prinrval.h"
#include "WorkerPrivate.h"
#include <algorithm>
#include <limits>

namespace mozilla {
namespace dom {

NS_IMPL_CYCLE_COLLECTION_CLASS(AbortSignal)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(AbortSignal,
                                                  DOMEventTargetHelper)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(AbortSignal,
                                                DOMEventTargetHelper)
  tmp->mReason.setUndefined();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(AbortSignal)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(AbortSignal, DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mReason)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_ADDREF_INHERITED(AbortSignal, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(AbortSignal, DOMEventTargetHelper)

AbortSignal::AbortSignal(nsIGlobalObject* aGlobalObject,
                         bool aAborted)
  : DOMEventTargetHelper(aGlobalObject)
  , mReason(JS::UndefinedValue())
  , mDefaultReason(NS_ERROR_DOM_ABORT_ERR)
  , mAborted(aAborted)
{
  mozilla::HoldJSObjects(this);
}

AbortSignal::AbortSignal(bool aAborted)
  : AbortSignal(nullptr, aAborted)
{}

AbortSignal::~AbortSignal()
{
  mozilla::DropJSObjects(this);
}

JSObject*
AbortSignal::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return AbortSignalBinding::Wrap(aCx, this, aGivenProto);
}

already_AddRefed<AbortSignal>
AbortSignal::Abort(GlobalObject& aGlobal, JS::Handle<JS::Value> aReason)
{
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  RefPtr<AbortSignal> signal = new AbortSignal(global, false);
  signal->SetAbortedReason(aReason);
  return signal.forget();
}

static bool
InitSignalJSAPI(AbortSignal* aSignal, AutoJSAPI& aJsapi)
{
  // DisconnectFromOwner clears DOMEventTargetHelper's weak parent. A retained
  // JS signal still has a reflector in its original realm after frame detach.
  if (JSObject* wrapper = aSignal->GetWrapper()) {
    return aJsapi.Init(wrapper);
  }
  return aJsapi.Init(aSignal->GetParentObject());
}

class AbortSignalTimeoutHandler final : public TimeoutHandler
{
public:
  AbortSignalTimeoutHandler(JSContext* aCx, AbortSignal* aSignal, uint64_t aDelay)
    : TimeoutHandler(aCx), mSignal(aSignal), mRemaining(aDelay) {}

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(AbortSignalTimeoutHandler, TimeoutHandler)

  void Schedule(JSContext* aCx, ErrorResult& aRv)
  {
    uint32_t limit = std::numeric_limits<int32_t>::max();
    if (NS_IsMainThread()) {
      limit = std::min(limit, PR_IntervalToMilliseconds(DELAY_INTERVAL_LIMIT));
    }
    int32_t delay = TakeAbortTimeoutSlice(mRemaining, limit);
    if (NS_IsMainThread()) {
      nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(mSignal->GetParentObject());
      if (!window) {
        aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
        return;
      }
      int32_t handle;
      nsresult rv = nsGlobalWindow::Cast(window)->SetTimeoutOrInterval(this,
        delay, false, Timeout::Reason::eAbortSignalTimeout, &handle);
      if (NS_FAILED(rv)) {
        aRv.Throw(rv);
      }
    } else {
      workers::WorkerPrivate* worker = workers::GetWorkerPrivateFromContext(aCx);
      if (!worker) {
        aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
        return;
      }
      worker->SetTimeout(aCx, this, delay, false, aRv,
                         Timeout::Reason::eAbortSignalTimeout);
    }
  }

  nsresult Call() override
  {
    // This runs on the global's timer task source. Owner teardown cancels the
    // timer; frozen globals pause the deadline and suspended globals defer
    // task dispatch through their existing lifecycle machinery.
    if (mRemaining) {
      AutoJSAPI jsapi;
      if (!InitSignalJSAPI(mSignal, jsapi)) {
        return NS_OK;
      }
      ErrorResult rv;
      Schedule(jsapi.cx(), rv);
      return rv.StealNSResult();
    }
    mSignal->Abort(NS_ERROR_DOM_TIMEOUT_ERR);
    return NS_OK;
  }

private:
  ~AbortSignalTimeoutHandler() = default;
  RefPtr<AbortSignal> mSignal;
  uint64_t mRemaining;
};

NS_IMPL_CYCLE_COLLECTION_INHERITED(AbortSignalTimeoutHandler, TimeoutHandler, mSignal)
NS_IMPL_ADDREF_INHERITED(AbortSignalTimeoutHandler, TimeoutHandler)
NS_IMPL_RELEASE_INHERITED(AbortSignalTimeoutHandler, TimeoutHandler)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(AbortSignalTimeoutHandler)
  NS_INTERFACE_MAP_ENTRY(nsITimeoutHandler)
NS_INTERFACE_MAP_END_INHERITING(TimeoutHandler)

already_AddRefed<AbortSignal>
AbortSignal::Timeout(GlobalObject& aGlobal, uint64_t aMilliseconds, ErrorResult& aRv)
{
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  RefPtr<AbortSignal> signal = new AbortSignal(global, false);
  RefPtr<AbortSignalTimeoutHandler> handler =
    new AbortSignalTimeoutHandler(aGlobal.Context(), signal, aMilliseconds);
  handler->Schedule(aGlobal.Context(), aRv);
  if (aRv.Failed()) {
    return nullptr;
  }
  return signal.forget();
}

void
AbortSignal::GetReason(JSContext* aCx, JS::MutableHandle<JS::Value> aReason,
                      ErrorResult& aRv)
{
  aRv.MightThrowJSException();
  aReason.setUndefined();
  if (!mAborted) {
    return;
  }
  if (mReason.isUndefined()) {
    // Keep default exceptions lazy. A reason must be stable even when abort()
    // happened before controller.signal was first requested.
    AutoJSAPI jsapi;
    if (!InitSignalJSAPI(this, jsapi)) {
      aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
      return;
    }
    JS::Rooted<JS::Value> reason(jsapi.cx());
    RefPtr<DOMException> exception = DOMException::Create(mDefaultReason);
    if (!ToJSValue(jsapi.cx(), exception, &reason)) {
      aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return;
    }
    mReason = reason;
  }
  aReason.set(mReason);
  if (!JS_WrapValue(aCx, aReason)) {
    aRv.StealExceptionFromJSContext(aCx);
  }
}

void
AbortSignal::ThrowIfAborted(ErrorResult& aRv)
{
  aRv.MightThrowJSException();
  if (!mAborted) {
    return;
  }
  AutoJSAPI jsapi;
  if (!InitSignalJSAPI(this, jsapi)) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }
  JS::Rooted<JS::Value> reason(jsapi.cx());
  GetReason(jsapi.cx(), &reason, aRv);
  if (!aRv.Failed()) {
    aRv.ThrowJSException(jsapi.cx(), reason);
  }
}

void
AbortSignal::RejectPromise(Promise* aPromise)
{
  AutoJSAPI jsapi;
  if (!jsapi.Init(aPromise->GetParentObject())) {
    aPromise->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }
  JS::Rooted<JS::Value> reason(jsapi.cx());
  ErrorResult rv;
  GetReason(jsapi.cx(), &reason, rv);
  rv.WouldReportJSException();
  if (rv.Failed()) {
    aPromise->MaybeReject(rv);
  } else {
    aPromise->MaybeReject(jsapi.cx(), reason);
  }
}

bool
AbortSignal::Aborted() const
{
  return mAborted;
}

void
AbortSignal::Abort(nsresult aDefaultReason)
{
  // Re-entrancy guard
  if (mAborted) {
    return;
  }
  mAborted = true;
  mDefaultReason = aDefaultReason;
  RunAbortSteps();
}

void
AbortSignal::SetAbortedReason(JS::Handle<JS::Value> aReason)
{
  MOZ_ASSERT(!mAborted);
  mAborted = true;
  mReason = aReason;
}

void
AbortSignal::SignalAbort(JS::Handle<JS::Value> aReason)
{
  if (mAborted) {
    return;
  }
  SetAbortedReason(aReason);
  RunAbortSteps();
}

void
AbortSignal::RunAbortSteps()
{
  // We might be deleted as a result of aborting a follower, so ensure we
  // stay alive until all followers have been aborted.
  RefPtr<AbortSignal> pinThis = this;

  // Let's inform the followers.
  // Followers can remove themselves or later followers during cancellation.
  {
    nsTObserverArray<Follower*>::ForwardIterator iter(mFollowers);
    while (iter.HasMore()) {
      // A callback may destroy its follower, so do not dereference it after
      // invoking the abort algorithm.
      iter.GetNext()->Aborted();
    }
  }
  // Algorithms are one-shot. A follower may still retain the signal until
  // asynchronous completion so the owning thread can read its exact reason;
  // its later Unfollow() safely removes an already-cleared registration.
  mFollowers.Clear();

  if (!GetParentObject()) {
    return;
  }
  EventInit init;
  init.mBubbles = false;
  init.mCancelable = false;

  RefPtr<Event> event =
    Event::Constructor(this, NS_LITERAL_STRING("abort"), init);
  event->SetTrusted(true);

  bool dummy;
  DispatchEvent(event, &dummy);
}

void
AbortSignal::AddFollower(AbortSignal::Follower* aFollower)
{
  MOZ_DIAGNOSTIC_ASSERT(aFollower);
  if (!mFollowers.Contains(aFollower)) {
    mFollowers.AppendElement(aFollower);
  }
}

void
AbortSignal::RemoveFollower(AbortSignal::Follower* aFollower)
{
  MOZ_DIAGNOSTIC_ASSERT(aFollower);
  mFollowers.RemoveElement(aFollower);
}

// AbortSignal::Follower
// ----------------------------------------------------------------------------

AbortSignal::Follower::~Follower()
{
  Unfollow();
}

void
AbortSignal::Follower::Follow(AbortSignal* aSignal)
{
  MOZ_DIAGNOSTIC_ASSERT(aSignal);

  Unfollow();

  mFollowingSignal = aSignal;
  aSignal->AddFollower(this);
}

void
AbortSignal::Follower::Unfollow()
{
  if (mFollowingSignal) {
    mFollowingSignal->RemoveFollower(this);
    mFollowingSignal = nullptr;
  }
}

} // dom namespace
} // mozilla namespace
