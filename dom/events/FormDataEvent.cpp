/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/FormDataEvent.h"

#include "mozilla/dom/FormData.h"

namespace mozilla {
namespace dom {

NS_IMPL_CYCLE_COLLECTION_INHERITED(FormDataEvent, Event, mFormData)
NS_IMPL_ADDREF_INHERITED(FormDataEvent, Event)
NS_IMPL_RELEASE_INHERITED(FormDataEvent, Event)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(FormDataEvent)
NS_INTERFACE_MAP_END_INHERITING(Event)

FormDataEvent::FormDataEvent(EventTarget* aOwner)
  : Event(aOwner, nullptr, nullptr)
{
}

FormDataEvent::~FormDataEvent() = default;

already_AddRefed<FormDataEvent>
FormDataEvent::Constructor(const GlobalObject& aGlobal, const nsAString& aType,
                          const FormDataEventInit& aInit, ErrorResult& aRv)
{
  nsCOMPtr<EventTarget> owner = do_QueryInterface(aGlobal.GetAsSupports());
  return Constructor(owner, aType, aInit);
}

already_AddRefed<FormDataEvent>
FormDataEvent::Constructor(EventTarget* aOwner, const nsAString& aType,
                          const FormDataEventInit& aInit)
{
  RefPtr<FormDataEvent> event = new FormDataEvent(aOwner);
  bool trusted = event->Init(aOwner);
  event->InitEvent(aType, aInit.mBubbles, aInit.mCancelable);
  event->mFormData = aInit.mFormData;
  event->SetTrusted(trusted);
  event->SetComposed(aInit.mComposed);
  return event.forget();
}

} // namespace dom
} // namespace mozilla
