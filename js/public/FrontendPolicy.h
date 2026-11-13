/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef js_FrontendPolicy_h
#define js_FrontendPolicy_h

#include "jstypes.h"

namespace JS {

// Process policy, atomically published for main-thread and helper parsers.
// Each parser snapshots it; existing scripts remain valid across transitions.
// This never enables lazy parsing when compilation options disallow it.
extern JS_PUBLIC_API(void)
SetLazyModuleExportsEnabled(bool enabled);

extern JS_PUBLIC_API(bool)
LazyModuleExportsEnabled();

} // namespace JS
#endif
