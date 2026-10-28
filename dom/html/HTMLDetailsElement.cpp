/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/HTMLDetailsElement.h"

#include "mozilla/dom/HTMLDetailsElementBinding.h"
#include "mozilla/dom/HTMLUnknownElement.h"
#include "mozilla/dom/ToggleEvent.h"
#include "mozilla/Preferences.h"

// Expand NS_IMPL_NS_NEW_HTML_ELEMENT(Details) to add pref check.
nsGenericHTMLElement*
NS_NewHTMLDetailsElement(already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo,
                         mozilla::dom::FromParser aFromParser)
{
  if (!mozilla::dom::HTMLDetailsElement::IsDetailsEnabled()) {
    return new mozilla::dom::HTMLUnknownElement(aNodeInfo);
  }

  return new mozilla::dom::HTMLDetailsElement(aNodeInfo);
}

namespace mozilla {
namespace dom {

/* static */ bool
HTMLDetailsElement::IsDetailsEnabled()
{
  static bool isDetailsEnabled = false;
  static bool added = false;

  if (!added) {
    Preferences::AddBoolVarCache(&isDetailsEnabled,
                                 "dom.details_element.enabled");
    added = true;
  }

  return isDetailsEnabled;
}

HTMLDetailsElement::~HTMLDetailsElement()
{
}

NS_IMPL_ELEMENT_CLONE(HTMLDetailsElement)

nsIContent*
HTMLDetailsElement::GetFirstSummary() const
{
  // XXX: Bug 1245032: Might want to cache the first summary element.
  for (nsIContent* child = nsINode::GetFirstChild();
       child;
       child = child->GetNextSibling()) {
    if (child->IsHTMLElement(nsGkAtoms::summary)) {
      return child;
    }
  }
  return nullptr;
}

nsChangeHint
HTMLDetailsElement::GetAttributeChangeHint(const nsIAtom* aAttribute,
                                           int32_t aModType) const
{
  nsChangeHint hint =
    nsGenericHTMLElement::GetAttributeChangeHint(aAttribute, aModType);
  if (aAttribute == nsGkAtoms::open) {
    hint |= nsChangeHint_ReconstructFrame;
  }
  return hint;
}

nsIAtom*
HTMLDetailsElement::GetDetailsName() const
{
  // Generic HTML attribute parsing interns nonempty names. HasName() cannot
  // be used here: it controls window/document named access for other tags.
  const nsAttrValue* name = GetParsedAttr(nsGkAtoms::name);
  return name && !name->IsEmptyString() ? name->GetAtomValue() : nullptr;
}

HTMLDetailsElement*
HTMLDetailsElement::FindOpenGroupMember(nsIAtom* aName) const
{
  MOZ_ASSERT(aName);
  nsINode* root = SubtreeRoot();
  for (nsINode* node = root; node; node = node->GetNextNode(root)) {
    if (node == this || !node->IsContent()) {
      continue;
    }
    HTMLDetailsElement* details = HTMLDetailsElement::FromContent(node->AsContent());
    if (details && details->Open() && details->GetDetailsName() == aName) {
      return details;
    }
  }
  return nullptr;
}

void
HTMLDetailsElement::CloseElementIfNeeded()
{
  if (!Open()) {
    return;
  }
  nsIAtom* name = GetDetailsName();
  if (name && FindOpenGroupMember(name)) {
    // Automatic closure is observable through MutationObserver and toggle,
    // but must not dispatch legacy synchronous mutation events.
    UnsetAttrWithoutLegacyMutationEvents(kNameSpaceID_None, nsGkAtoms::open, true);
  }
}

void
HTMLDetailsElement::CloseOtherElementsIfNeeded()
{
  MOZ_ASSERT(Open());
  nsIAtom* name = GetDetailsName();
  if (!name) {
    return;
  }
  RefPtr<HTMLDetailsElement> other = FindOpenGroupMember(name);
  if (other) {
    other->UnsetAttrWithoutLegacyMutationEvents(kNameSpaceID_None,
                                               nsGkAtoms::open, true);
  }
}

nsresult
HTMLDetailsElement::AfterSetAttr(int32_t aNameSpaceID, nsIAtom* aName,
                                 const nsAttrValue* aValue,
                                 const nsAttrValue* aOldValue,
                                 nsIPrincipal* aMaybeScriptedPrincipal,
                                 bool aNotify)
{
  nsresult rv = nsGenericHTMLElement::AfterSetAttr(aNameSpaceID, aName, aValue,
                                                  aOldValue,
                                                  aMaybeScriptedPrincipal,
                                                  aNotify);
  NS_ENSURE_SUCCESS(rv, rv);
  if (aNameSpaceID == kNameSpaceID_None) {
    if (aName == nsGkAtoms::name) {
      // Joining a group must preserve its already-open member.
      CloseElementIfNeeded();
    } else if (aName == nsGkAtoms::open && !!aValue != !!aOldValue) {
      ToggleEventInit init;
      init.mOldState.AssignASCII(aOldValue ? "open" : "closed");
      init.mNewState.AssignASCII(aValue ? "open" : "closed");
      if (mToggleEventDispatcher) {
        // Preserve the initial state of the pending task across coalescing.
        Event* event = mToggleEventDispatcher->mEvent->InternalDOMEvent();
        static_cast<ToggleEvent*>(event)->GetOldState(init.mOldState);
        mToggleEventDispatcher->Cancel();
      }
      RefPtr<ToggleEvent> event =
        ToggleEvent::Constructor(this, NS_LITERAL_STRING("toggle"), init);
      event->SetTrusted(true);
      mToggleEventDispatcher = new AsyncEventDispatcher(this, event);
      mToggleEventDispatcher->PostDOMEvent();
      if (aValue) {
        // Explicit opening wins over the existing member of this group.
        CloseOtherElementsIfNeeded();
      }
    }
  }
  return NS_OK;
}

nsresult
HTMLDetailsElement::BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                               nsIContent* aBindingParent,
                               bool aCompileEventHandlers)
{
  nsresult rv = nsGenericHTMLElement::BindToTree(aDocument, aParent,
                                                 aBindingParent,
                                                 aCompileEventHandlers);
  NS_ENSURE_SUCCESS(rv, rv);
  // Runs for parser insertion, detached subtrees, and moves into a new tree.
  // Attribute updates are script-blocked by the binding/insertion caller.
  CloseElementIfNeeded();
  return NS_OK;
}

void
HTMLDetailsElement::AsyncEventRunning(AsyncEventDispatcher* aEvent)
{
  if (mToggleEventDispatcher == aEvent) {
    mToggleEventDispatcher = nullptr;
  }
}

JSObject*
HTMLDetailsElement::WrapNode(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return HTMLDetailsElementBinding::Wrap(aCx, this, aGivenProto);
}

} // namespace dom
} // namespace mozilla
