// -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// Test root identity both before and after import into a fresh certificate DB.
"use strict";

function run_test() {
  do_get_profile();
  let certdb = Cc["@mozilla.org/security/x509certdb;1"]
                 .getService(Ci.nsIX509CertDB);
  const fixtureDir = "test_cert_isBuiltInRoot_reload/";
  const identities = JSON.parse(readFile(do_get_file(fixtureDir + "identities.json")));
  loginToDBWithDefaultPassword();

  for (let [role, expectedBuiltIn] of [["imported", true], ["nonbuiltin", false]]) {
    let info = identities[role];
    let cert = constructCertFromFile(fixtureDir + info.file);
    equal(cert.commonName, info.name, "fixture has the expected subject");
    equal(cert.sha256Fingerprint, info.sha256Fingerprint, "fixture has the expected DER");
    equal(cert.isBuiltInRoot, expectedBuiltIn, "constructed certificate has expected built-in identity");

    addCertFromFile(certdb, fixtureDir + info.file, ",,");
    let imported = certdb.findCertByDBKey(info.dbKey);
    ok(imported, "certificate can be found after import");
    equal(imported.sha256Fingerprint, info.sha256Fingerprint, "import preserves certificate identity");
    equal(imported.isBuiltInRoot, expectedBuiltIn, "import does not change built-in identity");
  }
}
