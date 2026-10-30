function getLastEventDetails(browser)
{
  return ContentTask.spawn(browser, {}, function* () {
    return content.document.getElementById('out').textContent;
  });
}

add_task(function* () {
  let onClickEvt = 'document.getElementById("out").textContent = event.target.localName + "," + event.clientX + "," + event.clientY;'
  const url = "<body onclick='" + onClickEvt + "' style='margin: 0'>" +
              "<button id='one' style='margin: 0; margin-left: 16px; margin-top: 14px; width: 30px; height: 40px;'>Test</button>" +
              "<div onmousedown='event.preventDefault()' style='margin: 0; width: 80px; height: 60px;'>Other</div>" +
              "<span id='out'></span></body>";
  let tab = yield BrowserTestUtils.openNewForegroundTab(gBrowser, "data:text/html," + url);

  let browser = tab.linkedBrowser;
  yield BrowserTestUtils.synthesizeMouseAtCenter("#one", {}, browser);
  let details = yield getLastEventDetails(browser);

  is(details, "button,31,34", "synthesizeMouseAtCenter");

  yield BrowserTestUtils.synthesizeMouse("#one", 4, 9, {}, browser);
  details = yield getLastEventDetails(browser);
  is(details, "button,20,23", "synthesizeMouse");

  yield BrowserTestUtils.synthesizeMouseAtPoint(15, 6, {}, browser);
  details = yield getLastEventDetails(browser);
  is(details, "body,15,6", "synthesizeMouseAtPoint on body");

  yield BrowserTestUtils.synthesizeMouseAtPoint(20, 22, {}, browser);
  details = yield getLastEventDetails(browser);
  is(details, "button,20,22", "synthesizeMouseAtPoint on button");

  yield BrowserTestUtils.synthesizeMouseAtCenter("body > div", {}, browser);
  details = yield getLastEventDetails(browser);
  is(details, "div,40,84", "synthesizeMouseAtCenter with complex selector");

  let cancelled = yield BrowserTestUtils.synthesizeMouseAtCenter("body > div", { type: "mousedown" }, browser);
  details = yield getLastEventDetails(browser);
  is(details, "div,40,84", "synthesizeMouseAtCenter mousedown with complex selector");
  ok(cancelled, "synthesizeMouseAtCenter mousedown with complex selector not cancelled");

  cancelled = yield BrowserTestUtils.synthesizeMouseAtCenter("body > div", { type: "mouseup" }, browser);
  details = yield getLastEventDetails(browser);
  is(details, "div,40,84", "synthesizeMouseAtCenter mouseup with complex selector");
  ok(!cancelled, "synthesizeMouseAtCenter mouseup with complex selector cancelled");

  gBrowser.removeTab(tab);
});

add_task(function* () {
  yield BrowserTestUtils.registerAboutPage(
    registerCleanupFunction, "about-pages-are-cool",
    getRootDirectory(gTestPath) + "dummy.html", 0);
  let tab = yield BrowserTestUtils.openNewForegroundTab(
    gBrowser, "about:about-pages-are-cool", true);
  ok(tab, "Successfully created an about: page and loaded it.");
  yield BrowserTestUtils.removeTab(tab);
  try {
    yield BrowserTestUtils.unregisterAboutPage("about-pages-are-cool");
    ok(true, "Successfully unregistered the about page.");
  } catch (ex) {
    ok(false, "Should not throw unregistering a known about: page");
  }
  yield BrowserTestUtils.unregisterAboutPage("random-other-about-page").then(() => {
    ok(false, "Should not have succeeded unregistering an unknown about: page.");
  }, () => {
    ok(true, "Should have returned a rejected promise trying to unregister an unknown about page");
  });
});

add_task(function* test_removeTab_lifecycle() {
  let tab = yield BrowserTestUtils.openNewForegroundTab(gBrowser, "about:blank");
  let otherTab = yield BrowserTestUtils.openNewForegroundTab(gBrowser, "about:blank");
  let mm = tab.linkedBrowser.messageManager;
  let finalMessageReceived = false;
  function onFinalMessage() {
    finalMessageReceived = true;
    mm.removeMessageListener("Test:RemoveTabFinal", onFinalMessage);
  }
  mm.addMessageListener("Test:RemoveTabFinal", onFinalMessage, true);
  let ready = BrowserTestUtils.waitForMessage(mm, "Test:RemoveTabReady");
  mm.loadFrameScript("data:,(" + function() {
    addEventListener("unload", function() {
      sendAsyncMessage("Test:RemoveTabFinal");
    });
    sendAsyncMessage("Test:RemoveTabReady");
  }.toString() + ")();", false);
  yield ready;

  let resolved = false;
  let removed = BrowserTestUtils.removeTab(tab, {dontRemove: true}).then(() => {
    resolved = true;
  });
  ok(!tab.closing, "dontRemove leaves the target tab open");
  yield BrowserTestUtils.removeTab(otherTab);
  ok(!resolved, "Another tab's disconnect must not complete removal");
  ok(!tab.closing, "dontRemove still leaves the target tab open");

  gBrowser.removeTab(tab);
  yield removed;
  ok(resolved, "External removal completes the pending wait");
  ok(!tab.linkedBrowser, "The browser has been detached before completion");
  ok(finalMessageReceived, "Final unload messages are delivered before completion");
});

add_task(function* test_removeTab_cancel_and_error_cleanup() {
  function observerCount() {
    let observers = Services.obs.enumerateObservers("message-manager-disconnect");
    let count = 0;
    while (observers.hasMoreElements()) {
      observers.getNext();
      count++;
    }
    return count;
  }

  // A cancelled beforeunload and a throwing removal must not leave a global
  // observer behind, waiting for an unrelated future destruction of the tab.
  let tabbrowser = { removeTab() {} };
  let tab = {
    linkedBrowser: { messageManager: gBrowser.selectedBrowser.messageManager },
    ownerDocument: { defaultView: { gBrowser: tabbrowser } },
    closing: false
  };
  let count = observerCount();
  yield BrowserTestUtils.removeTab(tab).then(() => {
    ok(false, "Cancelled removal must reject");
  }, ex => {
    is(ex.message, "Tab removal was cancelled.", "Cancelled removal rejects promptly");
  });
  is(observerCount(), count, "Cancelled removal removes its disconnect observer");

  let error = new Error("Removal failed");
  tabbrowser.removeTab = function() { throw error; };
  yield BrowserTestUtils.removeTab(tab).then(() => {
    ok(false, "Throwing removal must reject");
  }, ex => {
    is(ex, error, "Removal errors are propagated");
  });
  is(observerCount(), count, "Throwing removal removes its disconnect observer");
});

add_task(function* test_windowClosed_lifecycle() {
  let win = yield BrowserTestUtils.openNewBrowserWindow();
  registerCleanupFunction(function() {
    if (!win.closed) {
      win.close();
    }
  });
  yield BrowserTestUtils.openNewForegroundTab(win.gBrowser, "about:blank");
  let browsers = Array.from(win.gBrowser.browsers);
  is(browsers.length, 2, "The closing window contains two tabs");

  let received = new Set();
  let ready = [];
  browsers.forEach((browser, index) => {
    let mm = browser.messageManager;
    function onFinalMessage(message) {
      received.add(message.data.index);
      mm.removeMessageListener("Test:WindowClosedFinal", onFinalMessage);
    }
    mm.addMessageListener("Test:WindowClosedFinal", onFinalMessage, true);
    ready.push(BrowserTestUtils.waitForMessage(mm, "Test:WindowClosedReady"));
    mm.loadFrameScript("data:,(" + function(index) {
      addEventListener("unload", function() {
        sendAsyncMessage("Test:WindowClosedFinal", {index});
      });
      sendAsyncMessage("Test:WindowClosedReady");
    }.toString() + ")(" + index + ");", false);
  });
  yield Promise.all(ready);

  let closed = BrowserTestUtils.windowClosed(win);
  win.close();
  yield closed;
  ok(win.closed, "Window closure has completed");
  is(received.size, 2, "Both tabs' final unload messages arrive before completion");
  ok(received.has(0) && received.has(1), "Completion includes both distinct tabs");
});
