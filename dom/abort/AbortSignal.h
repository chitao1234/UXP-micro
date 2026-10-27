/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_AbortSignal_h
#define mozilla_dom_AbortSignal_h

#include "mozilla/DOMEventTargetHelper.h"
#include "nsTObserverArray.h"
#include "mozilla/ErrorResult.h"

namespace mozilla {
namespace dom {

class AbortSignal;
class Promise;

class AbortSignal final : public DOMEventTargetHelper
{
public:
  // This class must be implemented by objects who want to follow a AbortSignal.
  class Follower
  {
  public:
    virtual void Aborted() = 0;

  protected:
    virtual ~Follower();

    void
    Follow(AbortSignal* aSignal);

    void
    Unfollow();

    /* Should this be WeakRef? */
    RefPtr<AbortSignal> mFollowingSignal;
  };

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(AbortSignal, DOMEventTargetHelper)

  AbortSignal(nsIGlobalObject* aGlobalObject, bool aAborted);
  explicit AbortSignal(bool aAborted);

  JSObject*
  WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) override;

  bool
  Aborted() const;

  // Native cancellation transports can signal without a JS context. Default
  // exceptions are materialized lazily by GetReason on the owning thread.
  void Abort(nsresult aDefaultReason = NS_ERROR_DOM_ABORT_ERR);
  void SignalAbort(JS::Handle<JS::Value> aReason);
  void GetReason(JSContext* aCx, JS::MutableHandle<JS::Value> aReason,
                 ErrorResult& aRv);
  void ThrowIfAborted(ErrorResult& aRv);
  void RejectPromise(Promise* aPromise);

  IMPL_EVENT_HANDLER(abort);

  static already_AddRefed<AbortSignal> Abort(GlobalObject& aGlobal,
                                             JS::Handle<JS::Value> aReason);
  static already_AddRefed<AbortSignal> Timeout(GlobalObject& aGlobal,
                                               uint64_t aMilliseconds,
                                               ErrorResult& aRv);

  void
  AddFollower(Follower* aFollower);

  void
  RemoveFollower(Follower* aFollower);

private:
  friend class AbortController;
  ~AbortSignal();
  void SetAbortedReason(JS::Handle<JS::Value> aReason);
  void RunAbortSteps();

  // Raw pointers. Follower unregisters itself in the DTOR.
  nsTObserverArray<Follower*> mFollowers;

  JS::Heap<JS::Value> mReason;
  nsresult mDefaultReason;

  bool mAborted;
};

} // dom namespace
} // mozilla namespace

#endif // mozilla_dom_AbortSignal_h
