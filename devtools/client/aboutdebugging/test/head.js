/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/* eslint-env browser */
/* exported openAboutDebugging, changeAboutDebuggingHash, closeAboutDebugging,
   installAddon, uninstallAddon, waitForMutation, waitForContentMutation, assertHasTarget,
   getServiceWorkerList, getTabList, openPanel, waitForInitialAddonList,
   waitForServiceWorkerRegistered, unregisterServiceWorker,
   waitForDelayedStartupFinished, setupTestAboutDebuggingWebExtension,
   waitForServiceWorkerActivation, installAddonWithManager, createAddonXPI */
/* import-globals-from ../../framework/test/shared-head.js */

"use strict";

// Load the shared-head file first.
Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/framework/test/shared-head.js",
  this);

const { AddonManager } = Cu.import("resource://gre/modules/AddonManager.jsm", {});

flags.testing = true;
registerCleanupFunction(() => {
  flags.testing = false;
});

function* openAboutDebugging(page, win) {
  info("opening about:debugging");
  let url = "about:debugging";
  if (page) {
    url += "#" + page;
  }

  let tab = yield addTab(url, { window: win });
  let browser = tab.linkedBrowser;
  let document = browser.contentDocument;

  if (!document.querySelector(".app")) {
    yield waitForMutation(document.body, { childList: true });
  }

  return { tab, document };
}

/**
 * Change url hash for current about:debugging tab, return a promise after
 * new content is loaded.
 * @param  {DOMDocument}  document   container document from current tab
 * @param  {String}       hash       hash for about:debugging
 * @return {Promise}
 */
function changeAboutDebuggingHash(document, hash) {
  info(`Opening about:debugging#${hash}`);
  window.openUILinkIn(`about:debugging#${hash}`, "current");
  return waitForMutation(
    document.querySelector(".main-content"), {childList: true});
}

function openPanel(document, panelId) {
  info(`Opening ${panelId} panel`);
  document.querySelector(`[aria-controls="${panelId}"]`).click();
  return waitForMutation(
    document.querySelector(".main-content"), {childList: true});
}

function closeAboutDebugging(tab) {
  info("Closing about:debugging");
  return removeTab(tab);
}

function getSupportsFile(path) {
  let cr = Cc["@mozilla.org/chrome/chrome-registry;1"]
    .getService(Ci.nsIChromeRegistry);
  let uri = Services.io.newURI(CHROME_URL_ROOT + path, null, null);
  let fileurl = cr.convertChromeURL(uri);
  return fileurl.QueryInterface(Ci.nsIFileURL);
}

/**
 * Depending on whether there are addons installed, return either a target list
 * element or its container.
 * @param  {DOMDocument}  document   #addons section container document
 * @return {DOMNode}                 target list or container element
 */
function getAddonList(document) {
  return document.querySelector("#addons .target-list") ||
    document.querySelector("#addons .targets");
}

/**
 * Depending on whether there are service workers installed, return either a
 * target list element or its container.
 * @param  {DOMDocument}  document   #service-workers section container document
 * @return {DOMNode}                 target list or container element
 */
function getServiceWorkerList(document) {
  return document.querySelector("#service-workers .target-list") ||
    document.querySelector("#service-workers.targets");
}

/**
 * Depending on whether there are tabs opened, return either a
 * target list element or its container.
 * @param  {DOMDocument}  document   #tabs section container document
 * @return {DOMNode}                 target list or container element
 */
function getTabList(document) {
  return document.querySelector("#tabs .target-list") ||
    document.querySelector("#tabs.targets");
}

function createAddonXPI(files) {
  let directory = Services.dirsvc.get("TmpD", Ci.nsIFile);
  directory.append("aboutdebugging-addon");
  directory.createUnique(Ci.nsIFile.DIRECTORY_TYPE, 0o700);
  registerCleanupFunction(() => {
    if (directory.exists()) {
      directory.remove(true);
    }
  });
  let file = directory.clone();
  file.append("fixture.xpi");
  let zip = Cc["@mozilla.org/zipwriter;1"].createInstance(Ci.nsIZipWriter);
  zip.open(file, 0x02 | 0x08 | 0x20); // write/create/truncate
  try {
    for (let name of Object.keys(files)) {
      zip.addEntryFile(name, Ci.nsIZipWriter.COMPRESSION_NONE,
                       getSupportsFile(files[name]).file, false);
    }
  } finally {
    zip.close();
  }
  return file;
}

function installAddonWithManager(file) {
  return new Promise((resolve, reject) => {
    AddonManager.getInstallForFile(file, install => {
      if (!install || install.error) {
        reject(new Error("Cannot install add-on fixture: " +
                         (install ? install.error : "no install")));
        return;
      }
      let listener = {
        onDownloadFailed() { failed(); },
        onDownloadCancelled() { failed(); },
        onInstallFailed() { failed(); },
        onInstallCancelled() { failed(); },
        onInstallEnded(installation, addon) {
          install.removeListener(listener);
          // Register before resolving, so a later assertion/toolbox failure
          // cannot leave an installed fixture behind for the next test.
          registerCleanupFunction(function* () {
            let current = yield new Promise(done => AddonManager.getAddonByID(addon.id, done));
            if (current) {
              yield new Promise(done => {
                let cleanupListener = {
                  onUninstalled(removed) {
                    if (removed.id === addon.id) {
                      AddonManager.removeAddonListener(cleanupListener);
                      done();
                    }
                  }
                };
                AddonManager.addAddonListener(cleanupListener);
                current.uninstall();
              });
            }
          });
          resolve(addon);
        }
      };
      function failed() {
        install.removeListener(listener);
        reject(new Error("Add-on fixture installation failed: " + install.error));
      }
      install.addListener(listener);
      try {
        install.install();
      } catch (error) {
        install.removeListener(listener);
        reject(error);
      }
    });
  });
}

function* installAddon({document, path, name, isWebExtension, temporary = false}) {

  let addonList = getAddonList(document);
  let addonListMutation = waitForMutation(addonList, { childList: true });

  let onAddonInstalled;

  if (isWebExtension) {
    // WebExtensions were removed by 43d44975b1. Keep their historical helper
    // local to the explicitly skipped tests, not the supported worker/add-on
    // tests which share this head file.
    const { Management } = Cu.import("resource://gre/modules/Extension.jsm", {});
    onAddonInstalled = new Promise(done => {
      Management.on("startup", function listener(event, extension) {
        if (extension.name != name) {
          return;
        }

        Management.off("startup", listener);
        done();
      });
    });
  } else {
    // Wait for a "test-devtools" message sent by the addon's bootstrap.js file
    onAddonInstalled = new Promise(done => {
      let observed = false;
      function listener() {
        observed = true;
        Services.obs.removeObserver(listener, "test-devtools");
        done();
      }
      Services.obs.addObserver(listener, "test-devtools", false);
      registerCleanupFunction(() => {
        if (!observed) {
          Services.obs.removeObserver(listener, "test-devtools");
        }
      });
    });
  }
  let addon;
  if (isWebExtension || temporary) {
    // Only the explicitly skipped temporary-install tests use this path.
    let MockFilePicker = SpecialPowers.MockFilePicker;
    MockFilePicker.init(null);
    registerCleanupFunction(() => MockFilePicker.cleanup());
    MockFilePicker.returnFiles = [getSupportsFile(path).file];
    document.getElementById("load-addon-from-file").click();
  } else {
    // Bootstrap debugging is supported. Install the fixture normally through
    // Tycho's manager instead of relying on the unavailable temporary API.
    let directory = path.substring(0, path.lastIndexOf("/") + 1);
    let file = createAddonXPI({
      "install.rdf": path,
      "bootstrap.js": directory + "bootstrap.js"
    });
    addon = yield installAddonWithManager(file);
  }

  yield onAddonInstalled;
  ok(true, "Addon installed and running its bootstrap.js file");

  // Check that the addon appears in the UI
  yield addonListMutation;
  let names = [...addonList.querySelectorAll(".target-name")];
  names = names.map(element => element.textContent);
  ok(names.includes(name),
    "The addon name appears in the list of addons: " + names);
  return addon;
}

function* uninstallAddon({document, id, name}) {
  let addonList = getAddonList(document);
  let addonListMutation = waitForMutation(addonList, { childList: true });

  // Now uninstall this addon
  yield new Promise(done => {
    AddonManager.getAddonByID(id, addon => {
      let listener = {
        onUninstalled: function (uninstalledAddon) {
          if (uninstalledAddon != addon) {
            return;
          }
          AddonManager.removeAddonListener(listener);

          done();
        }
      };
      AddonManager.addAddonListener(listener);
      addon.uninstall();
    });
  });

  // Ensure that the UI removes the addon from the list
  yield addonListMutation;
  let names = [...addonList.querySelectorAll(".target-name")];
  names = names.map(element => element.textContent);
  ok(!names.includes(name),
    "After uninstall, the addon name disappears from the list of addons: "
    + names);
}

/**
 * Returns a promise that will resolve when the add-on list has been updated.
 *
 * @param {Node} document
 * @return {Promise}
 */
function waitForInitialAddonList(document) {
  const addonListContainer = getAddonList(document);
  let addonCount = addonListContainer.querySelectorAll(".target");
  addonCount = addonCount ? [...addonCount].length : -1;
  info("Waiting for add-ons to load. Current add-on count: " + addonCount);

  // This relies on the network speed of the actor responding to the
  // listAddons() request and also the speed of openAboutDebugging().
  let result;
  if (addonCount > 0) {
    info("Actually, the add-ons have already loaded");
    result = Promise.resolve();
  } else {
    result = waitForMutation(addonListContainer, { childList: true });
  }
  return result;
}

/**
 * Returns a promise that will resolve after receiving a mutation matching the
 * provided mutation options on the provided target.
 * @param {Node} target
 * @param {Object} mutationOptions
 * @return {Promise}
 */
function waitForMutation(target, mutationOptions) {
  return new Promise(resolve => {
    let observer = new MutationObserver(() => {
      observer.disconnect();
      resolve();
    });
    observer.observe(target, mutationOptions);
  });
}

/**
 * Returns a promise that will resolve after receiving a mutation in the subtree of the
 * provided target. Depending on the current React implementation, a text change might be
 * observable as a childList mutation or a characterData mutation.
 *
 * @param {Node} target
 * @return {Promise}
 */
function waitForContentMutation(target) {
  return waitForMutation(target, {
    characterData: true,
    childList: true,
    subtree: true
  });
}

/**
 * Checks if an about:debugging TargetList element contains a Target element
 * corresponding to the specified name.
 * @param {Boolean} expected
 * @param {Document} document
 * @param {String} type
 * @param {String} name
 */
function assertHasTarget(expected, document, type, name) {
  let names = [...document.querySelectorAll("#" + type + " .target-name")];
  names = names.map(element => element.textContent);
  is(names.includes(name), expected,
    "The " + type + " url appears in the list: " + names);
}

/**
 * Returns a promise that will resolve after the service worker in the page
 * has successfully registered itself.
 * @param {Tab} tab
 * @return {Promise} Resolves when the service worker is registered.
 */
function waitForServiceWorkerRegistered(tab) {
  return ContentTask.spawn(tab.linkedBrowser, {}, function* () {
    // Retrieve the `sw` promise created in the html page.
    let { sw } = content.wrappedJSObject;
    yield sw;
  });
}

/**
 * Asks the service worker within the test page to unregister, and returns a
 * promise that will resolve when it has successfully unregistered itself and the
 * about:debugging UI has fully processed this update.
 *
 * @param {Tab} tab
 * @param {Node} serviceWorkersElement
 * @return {Promise} Resolves when the service worker is unregistered.
 */
function* unregisterServiceWorker(tab, serviceWorkersElement) {
  let onMutation = waitForMutation(serviceWorkersElement, { childList: true });
  yield ContentTask.spawn(tab.linkedBrowser, {}, function* () {
    // Retrieve the `sw` promise created in the html page
    let { sw } = content.wrappedJSObject;
    let registration = yield sw;
    yield registration.unregister();
  });
  return onMutation;
}

/**
 * Waits for the creation of a new window, usually used with create private
 * browsing window.
 * Returns a promise that will resolve when the window is successfully created.
 * @param {window} win
 */
function waitForDelayedStartupFinished(win) {
  return new Promise(function (resolve) {
    Services.obs.addObserver(function observer(subject, topic) {
      if (win == subject) {
        Services.obs.removeObserver(observer, topic);
        resolve();
      }
    }, "browser-delayed-startup-finished", false);
  });
}

/**
 * open the about:debugging page and install an addon
 */
function* setupTestAboutDebuggingWebExtension(name, path) {
  yield new Promise(resolve => {
    let options = {"set": [
      // Force enabling of addons debugging
      ["devtools.chrome.enabled", true],
      ["devtools.debugger.remote-enabled", true],
      // Disable security prompt
      ["devtools.debugger.prompt-connection", false],
      // Enable Browser toolbox test script execution via env variable
      ["devtools.browser-toolbox.allow-unsafe-script", true],
    ]};
    SpecialPowers.pushPrefEnv(options, resolve);
  });

  let { tab, document } = yield openAboutDebugging("addons");
  yield waitForInitialAddonList(document);

  yield installAddon({
    document,
    path,
    name,
    isWebExtension: true,
  });

  // Retrieve the DEBUG button for the addon
  let names = [...document.querySelectorAll("#addons .target-name")];
  let nameEl = names.filter(element => element.textContent === name)[0];
  ok(name, "Found the addon in the list");
  let targetElement = nameEl.parentNode.parentNode;
  let debugBtn = targetElement.querySelector(".debug-button");
  ok(debugBtn, "Found its debug button");

  return { tab, document, debugBtn };
}

/**
 * Wait for aboutdebugging to be notified about the activation of the service worker
 * corresponding to the provided service worker url.
 */
function* waitForServiceWorkerActivation(swUrl, document) {
  let serviceWorkersElement = getServiceWorkerList(document);
  let names = serviceWorkersElement.querySelectorAll(".target-name");
  let name = [...names].filter(element => element.textContent === swUrl)[0];

  let targetElement = name.parentNode.parentNode;
  let targetStatus = targetElement.querySelector(".target-status");
  while (targetStatus.textContent === "Registering") {
    // Wait for the status to leave the "registering" stage.
    yield waitForMutation(serviceWorkersElement, { childList: true, subtree: true });
  }
}
