/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test that a Service Worker registration's Push Service subscription appears
// in about:debugging if it exists, and disappears when unregistered.

// Service workers can't be loaded from chrome://, but http:// is ok with
// dom.serviceWorkers.testing.enabled turned on.
const SERVICE_WORKER = URL_ROOT + "service-workers/push-sw.js";
const TAB_URL = URL_ROOT + "service-workers/push-sw.html";

const FAKE_ENDPOINT = "https://fake/endpoint";

const PushService = Cc["@mozilla.org/push/Service;1"]
  .getService(Ci.nsIPushService).wrappedJSObject;

add_task(function* () {
  info("Turn on workers via mochitest http.");
  yield SpecialPowers.pushPrefEnv({
    "set": [
      // Accept workers from mochitest's http.
      ["dom.serviceWorkers.enabled", true],
      ["dom.serviceWorkers.openWindow.enabled", true],
      ["dom.serviceWorkers.testing.enabled", true],
      // Enable the push service.
      ["dom.push.enabled", true],
      ["dom.push.connection.enabled", true],
    ]
  });

  info("Mock the push service");
  PushService.service = {
    _registrations: new Map(),
    _notify(scope) {
      Services.obs.notifyObservers(
        null,
        PushService.subscriptionModifiedTopic,
        scope);
    },
    init() {},
    register(pageRecord) {
      let registration = {
        endpoint: FAKE_ENDPOINT
      };
      this._registrations.set(pageRecord.scope, registration);
      this._notify(pageRecord.scope);
      return Promise.resolve(registration);
    },
    registration(pageRecord) {
      return Promise.resolve(this._registrations.get(pageRecord.scope));
    },
    unregister(pageRecord) {
      let deleted = this._registrations.delete(pageRecord.scope);
      if (deleted) {
        this._notify(pageRecord.scope);
      }
      return Promise.resolve(deleted);
    },
  };
  registerCleanupFunction(() => { PushService.service = null; });

  let { tab, document } = yield openAboutDebugging("workers");

  // Listen for mutations in the service-workers list.
  let serviceWorkersElement = document.getElementById("service-workers");
  let onMutation = waitForMutation(serviceWorkersElement, { childList: true });

  // Open a tab that registers a push service worker.
  let swTab = yield addTab(TAB_URL);

  // Wait for the service-workers list to update.
  yield onMutation;

  // Check that the service worker appears in the UI.
  assertHasTarget(true, document, "service-workers", SERVICE_WORKER);

  yield waitForServiceWorkerActivation(SERVICE_WORKER, document);

  // Wait for the service worker details to update.
  let names = [...document.querySelectorAll("#service-workers .target-name")];
  let name = names.filter(element => element.textContent === SERVICE_WORKER)[0];
  ok(name, "Found the service worker in the list");

  let targetContainer = name.parentNode.parentNode;
  let targetDetailsElement = targetContainer.querySelector(".target-details");

  // The shared page only registers the worker. As in newer Firefox, request
  // a subscription explicitly, after installing the mock and UI listeners.
  let detailsChanged = waitForMutation(targetDetailsElement, { childList: true });
  let endpoint = yield ContentTask.spawn(swTab.linkedBrowser, {}, function() {
    return content.wrappedJSObject.subscribeToPush();
  });
  is(endpoint, FAKE_ENDPOINT, "The page subscribed through the mocked Push service");
  yield detailsChanged;
  yield BrowserTestUtils.waitForCondition(() =>
    targetContainer.querySelector(".service-worker-push-url"));

  // Retrieve the push subscription endpoint URL, and verify it looks good.
  let pushURL = targetContainer.querySelector(".service-worker-push-url");

  ok(pushURL, "Found the push service URL in the service worker details");
  is(pushURL.textContent, FAKE_ENDPOINT, "The push service URL looks correct");

  // Unsubscribe from the push service.
  detailsChanged = waitForMutation(targetDetailsElement, { childList: true });
  let unsubscribed = yield ContentTask.spawn(swTab.linkedBrowser, {}, function() {
    return content.wrappedJSObject.unsubscribeToPush();
  });
  ok(unsubscribed, "The page unsubscribed from the mocked Push service");

  // Wait for the service worker details to update again.
  yield detailsChanged;
  yield BrowserTestUtils.waitForCondition(() =>
    !targetContainer.querySelector(".service-worker-push-url"));
  ok(!targetContainer.querySelector(".service-worker-push-url"),
    "The push service URL should be removed");

  // Finally, unregister the service worker itself.
  try {
    yield unregisterServiceWorker(swTab, serviceWorkersElement);
    ok(true, "Service worker registration unregistered");
  } catch (e) {
    ok(false, "SW not unregistered; " + e);
  }

  info("Unmock the push service");
  PushService.service = null;

  yield removeTab(swTab);
  yield closeAboutDebugging(tab);
});
