/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

// Permanent restartless add-ons remain supported by the Tycho Add-on Manager.
// Their Reload button must stay disabled: temporary installation/reload was
// not imported by deea787c2e (2018-02-10).
const ADDON_ID = "test-devtools@mozilla.org";
const ADDON_NAME = "test-devtools";

add_task(function* permanentlyInstalledAddonCannotBeReloaded() {
  const { tab, document } = yield openAboutDebugging("addons");
  yield waitForInitialAddonList(document);
  yield installAddon({
    document,
    path: "addons/unpacked/install.rdf",
    name: ADDON_NAME,
  });

  const name = [...document.querySelectorAll("#addons .target-name")]
    .find(element => element.textContent === ADDON_NAME);
  ok(name, "The normally installed bootstrap add-on is listed");
  const reloadButton = name.parentNode.parentNode.querySelector(".reload-button");
  ok(reloadButton, "Reload button exists");
  is(reloadButton.disabled, true, "A permanent add-on cannot be temporarily reloaded");
  ok(reloadButton.title, "Disabled reload button explains its unavailable action");

  yield uninstallAddon({document, id: ADDON_ID, name: ADDON_NAME});
  yield closeAboutDebugging(tab);
});
