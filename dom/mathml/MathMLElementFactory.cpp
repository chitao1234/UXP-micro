/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsContentCreatorFunctions.h"
#include "nsContentUtils.h"
#include "nsGkAtoms.h"
#include "nsNameSpaceManager.h"
#include "nsNodeInfoManager.h"
#include "mozilla/dom/MathMLElement.h"
#include "mozilla/dom/NodeInfo.h"

using namespace mozilla;
using namespace mozilla::dom;

// MathML Element Factory (declared in nsContentCreatorFunctions.h)
nsresult
NS_NewMathMLElement(Element** aResult, already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo)
{
  RefPtr<NodeInfo> nodeInfo = aNodeInfo;
  // Both the HTML parser and the generic XML/DOM factory reach this function.
  // Keep their MathML policy identical, including the system-document
  // exemption, without caching a preference across document/pref transitions.
  nsNameSpaceManager* nsmgr = nsNameSpaceManager::GetInstance();
  if (!(nsmgr && !nsmgr->mMathMLDisabled) &&
      !nsContentUtils::IsChromeDoc(nodeInfo->GetDocument())) {
    RefPtr<NodeInfo> disabledInfo = nodeInfo->NodeInfoManager()->GetNodeInfo(
      nodeInfo->NameAtom(), nodeInfo->GetPrefixAtom(),
      kNameSpaceID_disabled_MathML, nodeInfo->NodeType(),
      nodeInfo->GetExtraName());
    return NS_NewXMLElement(aResult, disabledInfo.forget());
  }

  NS_ADDREF(*aResult = new MathMLElement(nodeInfo.forget()));
  return NS_OK;
}
