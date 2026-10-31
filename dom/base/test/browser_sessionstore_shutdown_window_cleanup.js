/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

function makeDeferred() {
  let resolve;
  let promise = new Promise(done => { resolve = done; });
  return {promise, resolve};
}

function getBaseWindow(win) {
  return win.QueryInterface(Ci.nsIInterfaceRequestor)
            .getInterface(Ci.nsIDocShell)
            .QueryInterface(Ci.nsIDocShellTreeItem)
            .treeOwner.QueryInterface(Ci.nsIBaseWindow);
}

add_task(function* test_window_closes_during_flush() {
  let {SessionStore} = Cu.import(
    "resource:///modules/sessionstore/SessionStore.jsm", {});
  let {TabStateFlusher} = Cu.import(
    "resource:///modules/sessionstore/TabStateFlusher.jsm", {});
  let storeScope = Cu.getGlobalForObject(SessionStore);
  let store = storeScope.SessionStoreInternal;
  let flusher = Cu.getGlobalForObject(TabStateFlusher).TabStateFlusherInternal;

  let closingWindow;
  let liveWindow;
  let flushOutcome;
  let heldFlush = makeDeferred();
  let flushReady = makeDeferred();
  let originalFlush = flusher.flushWindow;
  let originalCollect = store._collectWindowData;
  let hadActiveWindowCache = "activeWindowSSiCache" in store;
  let originalActiveWindowCache = store.activeWindowSSiCache;
  let visibility = new Map();
  let collections = [];
  let cleanupKey = "shutdownWindowCleanupTest";
  let cleanupToken = Cc["@mozilla.org/uuid-generator;1"]
    .getService(Ci.nsIUUIDGenerator).generateUUID().toString();
  let undoPrefPushed = false;
  let cleanedUp = false;

  let cleanup = Task.async(function*() {
    if (cleanedUp) {
      return;
    }
    cleanedUp = true;
    heldFlush.resolve();
    if (flushOutcome) {
      yield flushOutcome;
    }
    flusher.flushWindow = originalFlush;
    store._collectWindowData = originalCollect;
    if (hadActiveWindowCache) {
      store.activeWindowSSiCache = originalActiveWindowCache;
    } else {
      delete store.activeWindowSSiCache;
    }
    try {
      for (let [win, visible] of visibility) {
        if (!win.closed) {
          getBaseWindow(win).visibility = visible;
        }
      }
      for (let win of [closingWindow, liveWindow]) {
        if (win && !win.closed) {
          yield BrowserTestUtils.closeWindow(win);
        }
        if (win && win.__SSi) {
          yield BrowserTestUtils.waitForCondition(() => !win.__SSi,
            "SessionStore has finished cleaning up the fixture window");
        }
      }
    } finally {
      try {
        let closed = JSON.parse(SessionStore.getClosedWindowData());
        for (let i = closed.length - 1; i >= 0; i--) {
          if (closed[i].extData && closed[i].extData[cleanupKey] == cleanupToken) {
            SessionStore.forgetClosedWindow(i);
          }
        }
      } finally {
        try {
          if (undoPrefPushed) {
            yield SpecialPowers.popPrefEnv();
          }
        } finally {
          visibility.clear();
          collections.length = 0;
          closingWindow = liveWindow = null;
          flushOutcome = heldFlush = flushReady = null;
        }
      }
    }
  });
  registerCleanupFunction(cleanup);

  try {
    // Tier 0 defers SessionStore. This is a component test, so activate it
    // through its public startup path before opening the first tracked window.
    // init() is single-use; use the actual registration flag rather than
    // RunState, which can still be stopped while initialization is in flight.
    if (!store._initialized) {
      SessionStore.init();
    }
    // Leave room for our two real closes so prior undo entries cannot be
    // evicted. Remove only our marked entries before restoring this capacity.
    let initialClosedCount = SessionStore.getClosedWindowCount();
    let currentUndoCap = Services.prefs.getIntPref("browser.sessionstore.max_windows_undo");
    yield SpecialPowers.pushPrefEnv({set: [["browser.sessionstore.max_windows_undo",
      Math.max(initialClosedCount, currentUndoCap) + 2]]});
    undoPrefPushed = true;
    closingWindow = yield BrowserTestUtils.openNewBrowserWindow();
    yield SessionStore.promiseInitialized;
    ok(storeScope.RunState.isRunning, "SessionStore reached its normal running state");
    SessionStore.setWindowValue(closingWindow, cleanupKey, cleanupToken);
    liveWindow = yield BrowserTestUtils.openNewBrowserWindow();
    SessionStore.setWindowValue(liveWindow, cleanupKey, cleanupToken);
    let browser = closingWindow.gBrowser.selectedBrowser;
    let url = "data:text/html;charset=utf-8," +
              encodeURIComponent("<title>flush-window-cleanup</title>saved window");
    let loaded = BrowserTestUtils.browserLoaded(browser, false, url);
    yield BrowserTestUtils.loadURI(browser, url);
    yield loaded;
    SessionStore.setWindowValue(closingWindow, "flushCleanup", "preserved");

    let closingID = closingWindow.__SSi;
    let liveID = liveWindow.__SSi;
    ok(!!closingID && !!store._windows[closingID], "Closing window is really tracked");
    ok(!!liveID && !!store._windows[liveID], "Live window is really tracked");
    store._forEachBrowserWindow(win => {
      visibility.set(win, getBaseWindow(win).visibility);
    });

    store._collectWindowData = function(win) {
      collections.push({win, tracked: !!(win.__SSi && this._windows[win.__SSi])});
      return originalCollect.call(this, win);
    };
    let heldOnce = false;
    flusher.flushWindow = function(win) {
      let promise = originalFlush.call(this, win);
      if (win == closingWindow && !heldOnce) {
        heldOnce = true;
        // Preserve the real frame-script flush. Hold only its completion so
        // onClose and its separate flush can finish before the caller resumes.
        return promise.then(() => {
          flushReady.resolve();
          return heldFlush.promise;
        });
      }
      return promise;
    };

    let progress = {};
    flushOutcome = store.flushAllWindowsAsync(progress).then(
      () => ({success: true}), error => ({success: false, error}));
    yield flushReady.promise;
    ok(heldOnce, "Actual shutdown flush reached the held completion boundary");
    is(progress.total, visibility.size, "Shutdown flush captured the real browser windows");

    yield BrowserTestUtils.closeWindow(closingWindow);
    yield BrowserTestUtils.waitForCondition(() => !closingWindow.__SSi,
      "SessionStore has finished cleaning up the closed window");
    ok(!store._windows[closingID], "onClose stopped tracking the closed window");
    let closedCollections = collections.filter(entry => entry.win == closingWindow);
    ok(closedCollections.length > 0, "onClose really collected the closing window");
    ok(closedCollections.every(entry => entry.tracked),
       "onClose collected while the window was tracked");
    let closedState = JSON.parse(SessionStore.getWindowState(closingWindow));
    is(closedState.windows[0].extData.flushCleanup, "preserved",
       "Closed-window state remains readable through the dying-window cache");
    is(closedState.windows[0].tabs[0].entries.slice(-1)[0].url, url,
       "onClose preserved the tab's actual loaded URL");

    heldFlush.resolve();
    let outcome = yield flushOutcome;
    ok(outcome.success, "Shutdown flush tolerates completed window cleanup" +
       (outcome.success ? "" : ": " + outcome.error));
    is(collections.filter(entry => entry.win == closingWindow).length,
       closedCollections.length, "Shutdown flush does not recollect the closed window");
    is(progress.current, progress.total, "Skipped closed windows still advance progress");
    let afterFlushState = JSON.parse(SessionStore.getWindowState(closingWindow));
    is(afterFlushState.windows[0].extData.flushCleanup, "preserved",
       "Finishing the flush preserves the already collected closed-window state");

    // A separate real flush makes this positive control independent of window
    // enumeration order, including when the unguarded implementation rejects.
    let liveCollections = collections.filter(entry => entry.win == liveWindow).length;
    let oldTabs = store._windows[liveID].tabs;
    let liveProgress = {};
    yield store.flushAllWindowsAsync(liveProgress);
    is(collections.filter(entry => entry.win == liveWindow).length,
       liveCollections + 1, "Still-tracked windows are collected by the real flush");
    isnot(store._windows[liveID].tabs, oldTabs, "Live-window tab data is freshly collected");
    is(liveProgress.current, liveProgress.total, "Live-window flush completes all progress");
    ok(storeScope.RunState.isRunning, "Regression did not change the global shutdown state");
  } finally {
    yield cleanup();
  }
});
