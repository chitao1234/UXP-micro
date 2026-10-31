/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
/* globals codemirrorSetStatus */

"use strict";

function runCodeMirrorTest(browser, {minimumTests = 1, requiredTests = []} = {}) {
  let mm = browser.messageManager;
  mm.addMessageListener("setStatus", function listener({data}) {
    let {statusMsg, type, customMsg} = data;
    codemirrorSetStatus(statusMsg, type, customMsg);
  });
  mm.addMessageListener("done", function listener({data}) {
    const validCounts = [data.totalTests, data.count, data.skipped, data.failed]
      .every(value => Number.isInteger(value) && value >= 0);
    const validRegistry = Array.isArray(data.registeredNames) &&
      data.registeredNames.every(name => typeof name == "string" && name.length > 0);
    ok(validCounts, "CodeMirror supplies real finite selected/executed/result counts");
    ok(validRegistry, "CodeMirror supplies its actual registered case names");
    if (validCounts && validRegistry) {
      ok(data.totalTests >= minimumTests,
         "CodeMirror retains the expected declared test population (minimum " + minimumTests + ")");
      is(data.totalTests, data.registeredNames.length,
         "The unfiltered suite selects every registered case");
      is(new Set(data.registeredNames).size, data.registeredNames.length,
         "Registered case names are unique");
      is(data.count, data.totalTests, "Every selected CodeMirror case executed");
      is(data.skipped, 0, "No selected CodeMirror case was skipped");
      is(data.failed, 0, "CodeMirror tests all passed");
      for (const name of requiredTests) {
        ok(data.registeredNames.indexOf(name) != -1,
           "The selected registry retains " + name);
      }
      info("CodeMirror completion " + JSON.stringify({
        selected: data.totalTests, executed: data.count,
        registered: data.registeredNames.length, skipped: data.skipped, failed: data.failed
      }));
    }
    while (gBrowser.tabs.length > 1) {
      gBrowser.removeCurrentTab();
    }
    mm = null;
    finish();
  });

  // Interact with the content iframe, giving it a function to
  //  1) Proxy CM test harness calls into ok() calls
  //  2) Detecting when it finishes by checking the DOM and
  //     setting a timeout to check again if not.
  mm.loadFrameScript("data:," +
    "content.wrappedJSObject.mozilla_setStatus = function(statusMsg, type, customMsg) {" +
    "  sendSyncMessage('setStatus', {statusMsg: statusMsg, type: type, customMsg: customMsg});" +
    "};" +
    "function check() { " +
    "  var doc = content.document; var out = doc.getElementById('status'); " +
    "  if (!out || !out.classList.contains('done')) { return setTimeout(check, 100); }" +
    "  var page = content.wrappedJSObject;" +
    "  sendAsyncMessage('done', { failed: page.failed, count: page.count," +
    "    totalTests: page.totalTests, skipped: page.skipped," +
    "    registeredNames: page.tests.map(function(test) { return test.name; }) });" +
    "}" +
    "check();"
  , true);
}
