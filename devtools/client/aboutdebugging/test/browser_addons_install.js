/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const ADDON_ID = "test-devtools@mozilla.org";
const ADDON_NAME = "test-devtools";

add_task(function* () {
  let { tab, document } = yield openAboutDebugging("addons");
  yield waitForInitialAddonList(document);

  // Normal bootstrap installation is supported by the Tycho manager.
  yield installAddon({
    document,
    path: "addons/unpacked/install.rdf",
    name: ADDON_NAME,
  });

  // Uninstall the add-on, and verify that it disappears in the UI.
  yield uninstallAddon({document, id: ADDON_ID, name: ADDON_NAME});

  yield closeAboutDebugging(tab);
});

add_task(function* () {
  let { tab, document } = yield openAboutDebugging("addons");
  yield waitForInitialAddonList(document);

  // deea787c2e imported Tycho's manager without temporary installation. The
  // UI must expose that capability accurately instead of throwing on click.
  is(typeof AddonManager.installTemporaryAddon, "undefined",
     "The manager has no temporary installation API");
  let button = document.getElementById("load-addon-from-file");
  ok(button, "The temporary-install control retains its label and location");
  ok(button.disabled, "Temporary installation is explicitly unavailable");
  let MockFilePicker = SpecialPowers.MockFilePicker;
  MockFilePicker.init(null);
  registerCleanupFunction(() => MockFilePicker.cleanup());
  let pickerShown = false;
  MockFilePicker.showCallback = () => { pickerShown = true; };
  button.click();
  yield new Promise(resolve => executeSoon(resolve));
  ok(!pickerShown, "The disabled control does not open a file picker");
  ok(!document.querySelector(".addons-install-error"),
     "An unavailable action does not produce a misleading install failure");
  yield closeAboutDebugging(tab);
});

add_task(function* invalidNormalAddonIsRejected() {
  // Invalid input is still a supported manager contract. Exercise an invalid
  // RDF XPI through getInstallForFile, independently of the absent temporary
  // installer and removed WebExtension manifest format.
  let file = createAddonXPI({"install.rdf": "addons/bad/install.rdf"});
  let install = yield new Promise(resolve => AddonManager.getInstallForFile(file, resolve));
  ok(install, "The manager reports an installation attempt for invalid input");
  is(install.state, AddonManager.STATE_DOWNLOAD_FAILED, "The malformed XPI is rejected");
  is(install.error, AddonManager.ERROR_CORRUPT_FILE, "Invalid RDF is classified as corrupt input");
  is(install.addon, null, "No add-on is installed for malformed input");
});
