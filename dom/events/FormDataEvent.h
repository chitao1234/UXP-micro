/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_FormDataEvent_h_
#define mozilla_dom_FormDataEvent_h_

#include "mozilla/dom/Event.h"
#include "mozilla/dom/FormDataEventBinding.h"

namespace mozilla {
namespace dom {

class FormData;

class FormDataEvent final : public Event
{
public:
  explicit FormDataEvent(EventTarget* aOwner);

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(FormDataEvent, Event)
  NS_FORWARD_TO_EVENT

  static already_AddRefed<FormDataEvent>
  Constructor(const GlobalObject& aGlobal, const nsAString& aType,
              const FormDataEventInit& aInit, ErrorResult& aRv);

  static already_AddRefed<FormDataEvent>
  Constructor(EventTarget* aOwner, const nsAString& aType,
              const FormDataEventInit& aInit);

  FormData* GetFormData() const { return mFormData; }

  JSObject* WrapObjectInternal(JSContext* aCx,
                              JS::Handle<JSObject*> aGivenProto) override
  {
    return FormDataEventBinding::Wrap(aCx, this, aGivenProto);
  }

protected:
  ~FormDataEvent();

private:
  RefPtr<FormData> mFormData;
};

} // namespace dom
} // namespace mozilla

#endif
