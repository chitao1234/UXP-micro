/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// This test uses chrome access to session-history viewers, and runs in the
// single-process configuration supported by this browser.
add_task(function* () {
  let base = getRootDirectory(gTestPath) + "file_history_budget.html";
  base = base.replace("chrome://mochitests/content", "http://example.com");
  let tab = gBrowser.addTab("about:blank");
  gBrowser.selectedTab = tab;
  let browser = tab.linkedBrowser;
  let extraTabs = [];
  let names = ["browser.memory.historyBudgetKB", "browser.memoryTier",
               "browser.sessionhistory.max_total_viewers"];
  let saved = names.map(name => [name, Services.prefs.prefHasUserValue(name),
                                Services.prefs.getIntPref(name, -1)]);
  function pref(name, value) { Services.prefs.setIntPref(name, value); }
  function show(action, expectedURI) {
    return new Promise((resolve, reject) => {
      let timer = setTimeout(() => {
        browser.removeEventListener("pageshow", listener, true);
        reject(new Error("Missing pageshow: " + browser.currentURI.spec));
      }, 15000);
      function listener(event) {
        if (event.target != browser.contentDocument) return;
        if (expectedURI && browser.currentURI.spec != expectedURI) return;
        clearTimeout(timer);
        browser.removeEventListener("pageshow", listener, true);
        let persisted = event.persisted;
        // pageshow precedes completion of some scrollbar/theme image loads.
        // Pending requests make a document ineligible for BFCache regardless
        // of its budget, so drain them before testing retention policy.
        let group = browser.docShell.QueryInterface(Ci.nsIDocumentLoader).loadGroup;
        BrowserTestUtils.waitForCondition(() => !group.requests.hasMoreElements(),
          "The loaded fixture has no pending requests before testing BFCache")
          .then(() => resolve(persisted), reject);
      }
      browser.addEventListener("pageshow", listener, true);
      action();
    });
  }
  function load(query) {
    return show(() => browser.loadURI(base + query), base + query);
  }
  function history() { return browser.webNavigation.sessionHistory; }
  function cached(query, whichBrowser = browser) {
    let h = whichBrowser.webNavigation.sessionHistory;
    for (let i = 0; i < h.count; i++) {
      let entry = h.getEntryAtIndex(i, false).QueryInterface(Ci.nsISHEntry);
      if (entry.URI.spec == base + query) {
        let owner = {};
        if (entry.getAnyContentViewer(owner)) return true;
      }
    }
    return false;
  }
  try {
    pref("browser.sessionhistory.max_total_viewers", 8);
    pref("browser.memoryTier", 3);
    pref("browser.memory.historyBudgetKB", -1);
    yield load("?small");
    let token = browser.contentWindow.wrappedJSObject.pageToken;
    yield load("?other");
    ok(cached("?small"), "Small page remains cached with the tier-3 policy");
    is(yield show(() => browser.goBack()), true, "Cached Back uses the presentation");
    is(browser.contentWindow.wrappedJSObject.pageToken, token, "Cached global survives");

    yield load("?heavy");
    browser.contentDocument.getElementById("field").value = "restored-value";
    browser.contentWindow.scrollTo(0, 300);
    token = browser.contentWindow.wrappedJSObject.pageToken;
    yield load("?after-heavy");
    ok(cached("?heavy"), "Heavy page is eligible before applying the byte budget");
    let count = history().count;
    pref("browser.memory.historyBudgetKB", 1024);
    ok(!cached("?heavy"), "Live preference change evicts the oversized presentation");
    is(history().count, count, "Eviction preserves history entries");
    is(yield show(() => browser.goBack()), false, "Evicted Back uses normal history loading");
    ok(browser.contentWindow.wrappedJSObject.pageToken != token, "Evicted global is reconstructed");
    is(browser.contentDocument.getElementById("field").value, "restored-value", "Form state survives eviction");
    is(browser.contentWindow.scrollY, 300, "Scroll state survives eviction");

    pref("browser.memory.historyBudgetKB", -1);
    pref("browser.memoryTier", 3);
    yield load("?alias");
    token = browser.contentWindow.wrappedJSObject.pageToken;
    browser.contentWindow.history.pushState({number: 1}, "", "#one");
    browser.contentWindow.history.pushState({number: 2}, "", "#two");
    let aliasEntries = [];
    for (let i = history().index - 2; i <= history().index; ++i) {
      let entry = history().getEntryAtIndex(i, false).QueryInterface(Ci.nsISHEntry);
      aliasEntries.push(entry.BFCacheEntry.ID);
    }
    ok(aliasEntries.every(id => id == aliasEntries[0]),
       "pushState entries share the original presentation identity");
    yield load("?after-alias");
    ok(cached("?alias#two"), "The aliased page is cached before traversing Back");
    is(yield show(() => browser.goBack()), true, "pushState aliases restore one cached presentation");
    is(browser.contentWindow.wrappedJSObject.pageToken, token, "Aliased viewer keeps its global");
    is(browser.contentWindow.history.state.number, 2, "pushState value survives");
    yield load("?after-alias-2");
    pref("browser.memory.historyBudgetKB", 0);
    ok(!cached("?alias#two"), "Zero budget clears aliased viewers");
    is(yield show(() => browser.goBack()), false, "Aliased entry reloads after eviction");
    is(browser.contentWindow.history.state.number, 2, "pushState value survives reload");

    pref("browser.memory.historyBudgetKB", -1);
    pref("browser.memoryTier", 3);
    yield load("?frame");
    yield load("?after-frame");
    ok(cached("?frame"), "Frame document is initially eligible");
    pref("browser.memory.historyBudgetKB", 1024);
    ok(!cached("?frame"), "Subdocument storage is charged to its parent viewer");

    pref("browser.memory.historyBudgetKB", -1);
    yield load("?tier-heavy");
    yield load("?after-tier-heavy");
    ok(cached("?tier-heavy"), "Tier-3 policy retains an eligible page");
    // Explicit positive policy remains effective across tier transitions.
    pref("browser.memory.historyBudgetKB", 1024);
    pref("browser.memoryTier", 1);
    ok(!cached("?tier-heavy"), "Explicit budget applies after changing tier");
    yield load("?retained-small");
    yield load("?after-small");
    ok(cached("?retained-small"), "Positive budget still retains small pages");
    pref("browser.memory.historyBudgetKB", 0);
    ok(!cached("?retained-small"), "Zero budget evicts even small presentations");

    pref("browser.memory.historyBudgetKB", -1);
    pref("browser.memoryTier", 1);
    yield load("?heavy-large");
    let tabSizes = Array.from({length: 9}, () => ({}));
    Cc["@mozilla.org/memory-reporter-manager;1"].getService(Ci.nsIMemoryReporterManager)
      .sizeOfTab(browser.contentWindow, ...tabSizes);
    let nonJSBytes = tabSizes[3].value + tabSizes[4].value + tabSizes[5].value;
    info("Large fixture non-JS bytes: " + nonJSBytes);
    ok(nonJSBytes > 8 * 1024 * 1024 && nonJSBytes < 24 * 1024 * 1024,
       "Fixture lies between the two default tier budgets");
    yield load("?after-large");
    ok(cached("?heavy-large"), "Tier 1 retains a presentation within its larger budget");
    pref("browser.memoryTier", 0);
    ok(!cached("?heavy-large"), "Tier 0 default byte budget evicts the large presentation");
    yield load("?tier-small");
    yield load("?after-tier-small");
    ok(cached("?tier-small"), "Tier 0 default still caches small presentations");

    pref("browser.memory.historyBudgetKB", 0);
    yield load("?start-global");
    pref("browser.memory.historyBudgetKB", 24576);
    yield load("?heavy-large-global-one");
    yield load("?heavy-large-global-two");
    yield load("?global-destination");
    ok(!cached("?heavy-large-global-one"), "Total byte budget evicts the farther presentation");
    ok(cached("?heavy-large-global-two"), "Total byte budget retains the nearer presentation");

    pref("browser.memory.historyBudgetKB", 0);
    yield load("?global-tabs-start");
    pref("browser.memory.historyBudgetKB", 24576);
    yield load("?heavy-large-tab-one");
    yield load("?tab-one-destination");
    let firstBrowser = browser;
    let secondTab = gBrowser.addTab("about:blank");
    extraTabs.push(secondTab);
    gBrowser.selectedTab = secondTab;
    browser = secondTab.linkedBrowser;
    yield load("?heavy-large-tab-two");
    yield load("?tab-two-destination");
    ok(!cached("?heavy-large-tab-one", firstBrowser), "Global budget evicts the older equal-distance viewer across tabs");
    ok(cached("?heavy-large-tab-two"), "Global budget retains the recently used tab's viewer");
    browser = firstBrowser;
    gBrowser.selectedTab = tab;
    gBrowser.removeTab(secondTab);
    extraTabs.pop();

    pref("browser.memory.historyBudgetKB", -1);
    pref("browser.memoryTier", 3);
    yield load("?frame");
    let parentToken = browser.contentWindow.wrappedJSObject.pageToken;
    let frame = browser.contentDocument.getElementById("child");
    yield new Promise(resolve => {
      frame.addEventListener("load", function listener() {
        frame.removeEventListener("load", listener);
        setTimeout(resolve, 0);
      });
      frame.contentWindow.location.href = base + "?frame-destination";
    });
    pref("browser.memory.historyBudgetKB", 1024);
    ok(!cached("?frame"), "No oversized child viewer remains after frame navigation");
    is(browser.contentWindow.wrappedJSObject.pageToken, parentToken,
       "Frame eviction leaves the active parent global alive");

    // Diagnostic timing uses a reverse-order control/arm sequence. The policy
    // deliberately trades heavy-page instant Back for reload; this measures
    // ordinary navigation overhead separately from that expected tradeoff.
    for (let budget of [-1, 8192, 8192, -1]) {
      pref("browser.memory.historyBudgetKB", 0);
      yield load("?benchmark-start");
      pref("browser.memoryTier", 3);
      pref("browser.memory.historyBudgetKB", budget);
      let times = [];
      for (let i = 0; i < 6; ++i) {
        yield load("?heavy-benchmark-" + i);
        let start = performance.now();
        yield load("?benchmark-destination-" + i);
        times.push(performance.now() - start);
      }
      info("Navigation timing " + JSON.stringify({budgetKB: budget, milliseconds: times}));
    }
  } finally {
    for (let extra of extraTabs) gBrowser.removeTab(extra);
    for (let [name, hadValue, value] of saved) {
      if (hadValue) pref(name, value); else Services.prefs.clearUserPref(name);
    }
    gBrowser.removeTab(tab);
  }
});
