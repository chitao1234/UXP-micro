/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// These exercise the application's tabbrowser implementation, which is also
// used by BrowserTestUtils. Browser origin attributes are established before
// insertion; progress callbacks may synchronously remove their own tab.
add_task(function* test_container_decoration_preserves_origin_attributes() {
  yield SpecialPowers.pushPrefEnv({set: [["privacy.userContext.enabled", true]]});
  let tabs = [];
  registerCleanupFunction(() => {
    for (let tab of tabs) {
      if (tab.parentNode && !tab.closing) {
        gBrowser.removeTab(tab);
      }
    }
  });

  for (let userContextId of [0, "0", 1, 2]) {
    let tab = gBrowser.addTab("about:blank", {userContextId});
    tabs.push(tab);
    let browser = tab.linkedBrowser;
    let hadAttribute = browser.hasAttribute("usercontextid");
    let attribute = browser.getAttribute("usercontextid");
    let expected = Number(userContextId);
    is(browser.contentPrincipal.originAttributes.userContextId, expected,
       "The browser is created in the requested container");

    // Repeated decoration, including preference/identity refreshes, must be
    // presentation-only. It cannot retarget an existing docshell's container.
    window.gContextualIdentity.updateTab(tab);
    window.gContextualIdentity.updateAllTabs();
    is(browser.hasAttribute("usercontextid"), hadAttribute,
       "Refreshing container UI preserves attribute presence");
    is(browser.getAttribute("usercontextid"), attribute,
       "Refreshing container UI preserves the exact attribute value");
    is(browser.contentPrincipal.originAttributes.userContextId, expected,
       "Refreshing container UI preserves principal origin attributes");
    gBrowser.removeTab(tab);
    tabs.pop();
  }
});

add_task(function* test_location_listener_can_remove_its_tab() {
  let tab = gBrowser.addTab("about:blank");
  let browser = tab.linkedBrowser;
  let listener = gBrowser._tabListeners.get(tab);
  let location = browser.currentURI;
  let callbackCount = 0;
  let failure;
  let tabsListener = {
    onLocationChange(changedBrowser, webProgress, request, uri) {
      if (changedBrowser !== browser) {
        return;
      }
      ++callbackCount;
      is(browser.lastURI, location, "Location state is current inside the callback");
      ok(browser.lastLocationChange >= 0, "The location timestamp is current inside the callback");
      gBrowser.removeTab(tab);
      ok(!listener.mBrowser, "Removing the tab destroys its progress listener");
    }
  };
  registerCleanupFunction(() => {
    gBrowser.removeTabsProgressListener(tabsListener);
    if (tab.parentNode && !tab.closing) {
      gBrowser.removeTab(tab);
    }
  });

  browser.lastURI = null;
  browser.lastLocationChange = -1;
  // This notification represents a normal, already-created document rather
  // than the newly inserted browser's suppressible initial about:blank load.
  listener.mBlank = false;
  gBrowser.addTabsProgressListener(tabsListener);
  try {
    // Invoke the real listener synchronously to make teardown reentrancy
    // deterministic, without depending on network/load timing.
    listener.onLocationChange(browser.webProgress, null, location,
      Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT);
  } catch (error) {
    failure = error;
  } finally {
    gBrowser.removeTabsProgressListener(tabsListener);
  }
  ok(!failure, "Location notification survives synchronous tab removal: " + failure);
  is(callbackCount, 1, "The closing listener is notified exactly once");
  ok(!listener.mBrowser, "The listener remains destroyed after notification returns");
});
