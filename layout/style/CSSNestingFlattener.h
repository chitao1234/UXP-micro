/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef CSSNestingFlattener_h
#define CSSNestingFlattener_h

#include "nsTArray.h"

class nsAString;

namespace mozilla {
namespace css {

// Offsets identify only the :is() functions introduced for nesting parents.
// Their lists must first be validated as ordinary (unforgiving) rule lists.
// This provenance is transient parser input, never stored with a stylesheet.
bool FlattenBasicCSSNesting(const nsAString& aInput, nsAString& aOutput,
                           nsTArray<uint32_t>& aParentGroupOffsets);

} // namespace css
} // namespace mozilla

#endif // CSSNestingFlattener_h
