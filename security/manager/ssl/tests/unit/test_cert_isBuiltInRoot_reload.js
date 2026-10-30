// -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// A preexisting SQL DB contains ISRG Root X1 and the non-built-in Let's Encrypt
// Authority X1. DigiCert Global Root G2 is available only from the built-in
// roots module. See test_cert_isBuiltInRoot_reload/README.md for regeneration.
"use strict";

function run_test() {
  const fixtureDir = "test_cert_isBuiltInRoot_reload/";
  let profile = do_get_profile();
  // This fork uses NSS's SQL database on desktop as well as Android, as modern
  // Firefox does. Copying the old cert8/key3 fixtures tests an empty SQL DB.
  for (let filename of ["cert9.db", "key4.db"]) {
    do_get_file(fixtureDir + filename).copyTo(profile, filename);
    let copied = profile.clone();
    copied.append(filename);
    ok(copied.exists(), "preexisting " + filename + " is in the profile before NSS initialization");
  }

  let certdb = Cc["@mozilla.org/security/x509certdb;1"]
                 .getService(Ci.nsIX509CertDB);
  const identities = JSON.parse(readFile(do_get_file(fixtureDir + "identities.json")));
  for (let [role, expectedBuiltIn] of [["moduleOnly", true], ["imported", true], ["nonbuiltin", false]]) {
    let info = identities[role];
    // Do not construct temporary certificates first: find the records from the
    // existing DB or the roots module, using independently generated DB keys.
    let cert = certdb.findCertByDBKey(info.dbKey);
    ok(cert, role + " certificate is available after loading the preexisting DB");
    equal(cert.commonName, info.name, role + " certificate has the expected subject");
    equal(cert.sha256Fingerprint, info.sha256Fingerprint, role + " certificate has the expected DER");
    equal(cert.isBuiltInRoot, expectedBuiltIn, role + " certificate retains expected built-in identity");
  }
}
