/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ToggleEvent_h_
#define mozilla_dom_ToggleEvent_h_

#include "mozilla/dom/Event.h"
#include "mozilla/dom/ToggleEventBinding.h"

namespace mozilla {
namespace dom {

class Element;

class ToggleEvent final : public Event
{
public:
  explicit ToggleEvent(EventTarget* aOwner);

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(ToggleEvent, Event)
  NS_FORWARD_TO_EVENT

  static already_AddRefed<ToggleEvent>
  Constructor(const GlobalObject& aGlobal, const nsAString& aType,
              const ToggleEventInit& aInit, ErrorResult& aRv);

  static already_AddRefed<ToggleEvent>
  Constructor(EventTarget* aOwner, const nsAString& aType,
              const ToggleEventInit& aInit);

  void GetOldState(nsString& aResult) const { aResult = mOldState; }
  void GetNewState(nsString& aResult) const { aResult = mNewState; }
  Element* GetSource();

  JSObject* WrapObjectInternal(JSContext* aCx,
                              JS::Handle<JSObject*> aGivenProto) override
  {
    return ToggleEventBinding::Wrap(aCx, this, aGivenProto);
  }

protected:
  ~ToggleEvent() {}

private:
  nsString mOldState;
  nsString mNewState;
  RefPtr<Element> mSource;
};

} // namespace dom
} // namespace mozilla

#endif
