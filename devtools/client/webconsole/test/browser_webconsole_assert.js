/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Test that console.assert() works as expected (i.e. outputs only on falsy
// asserts). See bug 760193.

"use strict";

const TEST_URI = "http://example.com/browser/devtools/client/webconsole/" +
                 "test/test-console-assert.html";

add_task(function* () {
  is(Services.prefs.getBoolPref("browser.internal-userscripts.debug"), false,
     "Bundled polyfill diagnostics are disabled by default");
  yield loadTab(TEST_URI);

  let polyfillType = yield ContentTask.spawn(gBrowser.selectedBrowser, null,
    function () {
      return typeof content.wrappedJSObject.Intl.ListFormat;
    });
  is(polyfillType, "function", "Bundled polyfills remain available with diagnostics disabled");

  let hud = yield openConsole();
  yield consoleOpened(hud);
});

add_task(function* testBundledPolyfillDiagnostics() {
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("browser.internal-userscripts.debug");
  });
  Services.prefs.setBoolPref("browser.internal-userscripts.debug", true);
  yield loadTab(TEST_URI);
  let hud = yield openConsole();
  yield waitForMessages({
    webconsole: hud,
    messages: [{
      text: "[internal userscripts] Loaded polyfill script: Intl.ListFormat (bundled)",
      category: CATEGORY_WEBDEV,
      severity: SEVERITY_INFO,
    }],
  });
  Services.prefs.clearUserPref("browser.internal-userscripts.debug");
});

function consoleOpened(hud) {
  hud.jsterm.execute("test()");

  return waitForMessages({
    webconsole: hud,
    messages: [{
      text: "undefined",
      category: CATEGORY_OUTPUT,
      severity: SEVERITY_LOG,
    },
    {
      text: "start",
      category: CATEGORY_WEBDEV,
      severity: SEVERITY_LOG,
    },
    {
      text: "false assert",
      category: CATEGORY_WEBDEV,
      severity: SEVERITY_ERROR,
    },
    {
      text: "falsy assert",
      category: CATEGORY_WEBDEV,
      severity: SEVERITY_ERROR,
    },
    {
      text: "end",
      category: CATEGORY_WEBDEV,
      severity: SEVERITY_LOG,
    }],
  }).then(() => {
    let messages = [...hud.outputNode.querySelectorAll(".message")];
    ok(!messages.some(node => node.textContent.includes("true assert")),
       "the true assertion produces no console message");
  });
}
