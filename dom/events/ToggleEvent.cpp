/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ToggleEvent.h"

#include "mozilla/dom/Element.h"
#include "nsContentUtils.h"
#include "nsINode.h"

namespace mozilla {
namespace dom {

NS_IMPL_CYCLE_COLLECTION_INHERITED(ToggleEvent, Event, mSource)
NS_IMPL_ADDREF_INHERITED(ToggleEvent, Event)
NS_IMPL_RELEASE_INHERITED(ToggleEvent, Event)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ToggleEvent)
NS_INTERFACE_MAP_END_INHERITING(Event)

ToggleEvent::ToggleEvent(EventTarget* aOwner)
  : Event(aOwner, nullptr, nullptr)
{
}

already_AddRefed<ToggleEvent>
ToggleEvent::Constructor(const GlobalObject& aGlobal, const nsAString& aType,
                          const ToggleEventInit& aInit, ErrorResult& aRv)
{
  nsCOMPtr<EventTarget> owner = do_QueryInterface(aGlobal.GetAsSupports());
  return Constructor(owner, aType, aInit);
}

already_AddRefed<ToggleEvent>
ToggleEvent::Constructor(EventTarget* aOwner, const nsAString& aType,
                          const ToggleEventInit& aInit)
{
  RefPtr<ToggleEvent> event = new ToggleEvent(aOwner);
  bool trusted = event->Init(aOwner);
  event->InitEvent(aType, aInit.mBubbles, aInit.mCancelable);
  event->mOldState = aInit.mOldState;
  event->mNewState = aInit.mNewState;
  event->mSource = aInit.mSource;
  event->SetTrusted(trusted);
  event->SetComposed(aInit.mComposed);
  return event.forget();
}

Element*
ToggleEvent::GetSource()
{
  nsCOMPtr<nsINode> currentTarget = do_QueryInterface(Event::GetCurrentTarget());
  nsINode* source = nsContentUtils::Retarget(mSource, currentTarget);
  return source ? source->AsElement() : nullptr;
}

} // namespace dom
} // namespace mozilla
