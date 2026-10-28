/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_HTMLDetailsElement_h
#define mozilla_dom_HTMLDetailsElement_h

#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/Attributes.h"
#include "nsGenericHTMLElement.h"

namespace mozilla {
namespace dom {

// HTMLDetailsElement implements the <details> tag, which is used as a
// disclosure widget from which the user can obtain additional information or
// controls. Please see the spec for more information.
// https://html.spec.whatwg.org/multipage/forms.html#the-details-element
//
class HTMLDetailsElement final : public nsGenericHTMLElement
{
public:
  using NodeInfo = mozilla::dom::NodeInfo;

  static bool IsDetailsEnabled();

  explicit HTMLDetailsElement(already_AddRefed<NodeInfo>& aNodeInfo)
    : nsGenericHTMLElement(aNodeInfo)
  {
  }

  NS_IMPL_FROMCONTENT_HTML_WITH_TAG(HTMLDetailsElement, details)

  nsIContent* GetFirstSummary() const;

  nsresult Clone(NodeInfo* aNodeInfo, nsINode** aResult) const override;

  nsChangeHint GetAttributeChangeHint(const nsIAtom* aAttribute,
                                      int32_t aModType) const override;

  nsresult AfterSetAttr(int32_t aNameSpaceID, nsIAtom* aName,
                       const nsAttrValue* aValue, const nsAttrValue* aOldValue,
                       nsIPrincipal* aMaybeScriptedPrincipal,
                       bool aNotify) override;

  nsresult BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                      nsIContent* aBindingParent,
                      bool aCompileEventHandlers) override;

  // HTMLDetailsElement WebIDL
  void GetName(nsAString& aName) const
  {
    GetHTMLAttr(nsGkAtoms::name, aName);
  }

  void SetName(const nsAString& aName, ErrorResult& aError)
  {
    SetHTMLAttr(nsGkAtoms::name, aName, aError);
  }

  bool Open() const { return GetBoolAttr(nsGkAtoms::open); }

  void SetOpen(bool aOpen, ErrorResult& aError)
  {
    SetHTMLBoolAttr(nsGkAtoms::open, aOpen, aError);
  }

  void ToggleOpen()
  {
    ErrorResult rv;
    SetOpen(!Open(), rv);
    rv.SuppressException();
  }

  virtual void AsyncEventRunning(AsyncEventDispatcher* aEvent) override;

protected:
  virtual ~HTMLDetailsElement();

  // Group membership is scoped to the ordinary DOM tree, including detached
  // subtrees and shadow roots. No document table retains details elements.
  nsIAtom* GetDetailsName() const;
  HTMLDetailsElement* FindOpenGroupMember(nsIAtom* aName) const;
  void CloseElementIfNeeded();
  void CloseOtherElementsIfNeeded();

  JSObject* WrapNode(JSContext* aCx,
                     JS::Handle<JSObject*> aGivenProto) override;

  RefPtr<AsyncEventDispatcher> mToggleEventDispatcher;
};

} // namespace dom
} // namespace mozilla

#endif /* mozilla_dom_HTMLDetailsElement_h */
