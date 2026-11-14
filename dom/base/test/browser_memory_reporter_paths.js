/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Follow the reporter-contract checks in upstream test_memoryReporters.xhtml,
// with coverage for the local overlapping stylesheet diagnostics.
add_task(function* test_memory_reporter_paths() {
  let url = "data:text/html," + encodeURIComponent(
    "<!doctype html><style>:root { --reporter-test: 7px }" +
    "div { padding: var(--reporter-test); color: red }</style><div>report</div>");
  let tab = yield BrowserTestUtils.openNewForegroundTab(gBrowser, url);
  try {
    yield ContentTask.spawn(tab.linkedBrowser, null, function() {
      is(content.getComputedStyle(content.document.querySelector("div")).paddingTop,
         "7px", "The fixture has computed its custom property");
    });
    let manager = Cc["@mozilla.org/memory-reporter-manager;1"]
      .getService(Ci.nsIMemoryReporterManager);
    let rows = [];
    yield new Promise(resolve => {
      manager.getReports((process, path, kind, units, amount) => {
        rows.push({ process, path, kind, units, amount });
      }, null, resolve, null, false);
    });

    let explicit = rows.filter(r => r.path.startsWith("explicit/"));
    ok(explicit.length > 0, "Collected real owning memory reports");
    let invalid = explicit.filter(r =>
      (r.kind !== Ci.nsIMemoryReporter.KIND_HEAP &&
       r.kind !== Ci.nsIMemoryReporter.KIND_NONHEAP) ||
      r.units !== Ci.nsIMemoryReporter.UNITS_BYTES);
    is(invalid.length, 0,
       "Every explicit report obeys about:memory's contract: " +
       invalid.map(r => r.path).join(", "));

    let details = rows.filter(r => r.path.startsWith("style-sheet-details/"));
    ok(details.length > 0, "Per-window stylesheet diagnostics are present");
    ok(details.every(r => r.kind === Ci.nsIMemoryReporter.KIND_OTHER &&
                         r.units === Ci.nsIMemoryReporter.UNITS_BYTES),
       "Stylesheet diagnostics remain alternate byte measurements");
    let refs = rows.find(r => r.path === "css-authored-variables/references");
    ok(refs && refs.kind === Ci.nsIMemoryReporter.KIND_OTHER &&
       refs.units === Ci.nsIMemoryReporter.UNITS_COUNT && refs.amount > 0,
       "Authored-variable references are a separate population measurement");
  } finally {
    yield BrowserTestUtils.removeTab(tab);
  }
});
