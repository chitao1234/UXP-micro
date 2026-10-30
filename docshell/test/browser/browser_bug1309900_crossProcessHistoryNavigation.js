/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

add_task(function* runTests() {
  let tab = yield BrowserTestUtils.openNewForegroundTab(gBrowser, "about:about");

  registerCleanupFunction(function* () {
    gBrowser.removeTab(tab);
  });

  let browser = tab.linkedBrowser;

  // Use a supported privileged about page. Firefox Accounts is not part of
  // this application, while about:config exercises the same history path.
  let loaded = BrowserTestUtils.browserLoaded(browser, false, "about:config");
  browser.loadURI("about:config");
  let href = yield loaded;
  is(href, "about:config", "Check about:config loaded");

  // Using a dummy onunload listener to disable the bfcache as that can prevent
  // the test browser load detection mechanism from working.
  let dataURI = "data:text/html,<body%20onunload=''><iframe></iframe></body>";
  loaded = BrowserTestUtils.browserLoaded(browser, false, dataURI);
  browser.loadURI(dataURI);
  href = yield loaded;
  is(href, dataURI,
    "Check data URL loaded");

  loaded = BrowserTestUtils.browserLoaded(browser, false, "about:config");
  browser.goBack();
  href = yield loaded;
  is(href, "about:config", "Check we've gone back to about:config");

  loaded = BrowserTestUtils.browserLoaded(browser, false, dataURI);
  browser.goForward();
  href = yield loaded;
  is(href, dataURI,
     "Check we've gone forward to data URL");
});
