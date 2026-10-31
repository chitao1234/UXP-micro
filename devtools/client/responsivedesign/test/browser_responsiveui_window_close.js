/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(function* normal_close() {
  let tab = yield addTab("data:text/html;charset=utf-8,Live RDM close");
  let {rdm} = yield openRDM(tab);
  let client = rdm.client;
  let off = waitForRDMEvent("off", tab);
  let resized = waitForRDMEvent("content-resize", tab);
  let closed = waitForClientClosed(client);
  let stopped = new Promise(resolve => {
    let mm = rdm.mm;
    let listener = message => {
      mm.removeMessageListener("ResponsiveMode:Stop:Done", listener);
      resolve(message);
    };
    mm.addMessageListener("ResponsiveMode:Stop:Done", listener);
    registerCleanupFunction(() =>
      mm.removeMessageListener("ResponsiveMode:Stop:Done", listener));
  });

  yield rdm.close();
  is((yield resized).tab, tab, "Live close receives the target tab's resize");
  is((yield stopped).name, "ResponsiveMode:Stop:Done",
     "Live close receives the child's actual stop acknowledgment");
  yield off;
  yield closed;
  checkClosed(rdm, tab, client, "Live close");
  yield removeTab(tab);
});

add_task(function* tab_close() {
  let tab = yield addTab("data:text/html;charset=utf-8,Native RDM tab close");
  let {rdm} = yield openRDM(tab);
  let client = rdm.client;
  let off = waitForRDMEvent("off", tab);
  let closed = waitForClientClosed(client);

  ok(ResponsiveUIManager.isActiveForTab(tab),
     "ResponsiveUI should be active for the initialized tab being closed");
  yield removeTab(tab);
  yield off;
  yield closed;
  checkClosed(rdm, tab, client, "Tab close");
});

add_task(function* window_close() {
  let newWindowPromise = BrowserTestUtils.waitForNewWindow();
  window.open("about:blank", "_blank");
  let newWindow = yield newWindowPromise;
  registerCleanupFunction(function* () {
    if (!newWindow.closed) {
      yield BrowserTestUtils.closeWindow(newWindow);
    }
  });

  newWindow.focus();
  yield once(newWindow.gBrowser, "load", true);

  let tab = newWindow.gBrowser.selectedTab;
  let rdm = yield ResponsiveUIManager.openIfNeeded(newWindow, tab);
  let client = rdm.client;
  let closed = waitForClientClosed(client);

  // Close the window on a tab with an active responsive design UI and
  // wait for the UI to gracefully shutdown.  This has leaked the window
  // in the past.
  ok(ResponsiveUIManager.isActiveForTab(tab),
     "ResponsiveUI should be active for tab when the window is closed");
  let offPromise = waitForRDMEvent("off", tab);
  yield BrowserTestUtils.closeWindow(newWindow);
  yield offPromise;
  yield closed;
  checkClosed(rdm, tab, client, "Window close");
});

function waitForRDMEvent(type, tab) {
  return new Promise(resolve => {
    let listener = (event, data) => {
      if (data.tab !== tab) {
        return;
      }
      ResponsiveUIManager.off(type, listener);
      resolve(data);
    };
    ResponsiveUIManager.on(type, listener);
    registerCleanupFunction(() => ResponsiveUIManager.off(type, listener));
  });
}

function waitForClientClosed(client) {
  return new Promise(resolve => {
    let listener = () => {
      client.removeListener("closed", listener);
      resolve();
    };
    client.addListener("closed", listener);
    registerCleanupFunction(() => client.removeListener("closed", listener));
  });
}

function checkClosed(rdm, tab, client, label) {
  ok(!ResponsiveUIManager.isActiveForTab(tab), label + " removes the active tab");
  is(ResponsiveUIManager.getResponsiveUIForTab(tab), undefined,
     label + " removes the manager's UI entry");
  ok(client._closed, label + " closes the captured debugger client");
  is(client._transport, null, label + " releases the client transport");
  is(rdm.client, null, label + " releases the UI's client reference");
  is(rdm.emulationFront, null, label + " releases the emulation front");
}
