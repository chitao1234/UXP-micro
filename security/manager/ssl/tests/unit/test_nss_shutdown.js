/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

// This test attempts to ensure that PSM doesn't deadlock or crash when shutting
// down NSS while a background thread is attempting to use NSS.
// UXP c6c045a7f7 removed signed-app verification. Certificate verification
// still uses CryptoTask, including its shutdown lock and main-thread callback.

var completedVerifications = 0;
function startAsyncNSSOperation(certdb, cert) {
  return new Promise(resolve => {
    certdb.asyncVerifyCertAtTime(cert, certificateUsageSSLServer,
      Ci.nsIX509CertDB.FLAG_LOCAL_ONLY, "example.com",
      Math.floor(Date.now() / 1000), function(errorCode, chain, hasEVPolicy) {
        // A completed verification or the CryptoTask shutdown error are both
        // valid: every dispatched task must deliver exactly one callback.
        equal(typeof errorCode, "number", "pending verification reports a result");
        completedVerifications++;
        resolve();
      });
  });
}

add_task(function* () {
  do_get_profile();
  let psm = Cc["@mozilla.org/psm;1"]
              .getService(Ci.nsISupports)
              .QueryInterface(Ci.nsIObserver);
  let certdb = Cc["@mozilla.org/security/x509certdb;1"]
                 .getService(Ci.nsIX509CertDB);
  let cert = constructCertFromFile("bad_certs/default-ee.pem");

  let promises = [];
  for (let i = 0; i < 25; i++) {
    promises.push(startAsyncNSSOperation(certdb, cert));
  }
  // Trick PSM into thinking it should shut down NSS. If this test doesn't
  // hang or crash, we're good.
  psm.observe(null, "profile-before-change", null);
  // AsyncVerifyCertAtTime checks isAlreadyShutDown() before dispatch and
  // rejects new work synchronously. Previously queued CryptoTasks still call
  // back, even when their calculation was skipped by NSS shutdown.
  for (let i = 0; i < 25; i++) {
    throws(() => certdb.asyncVerifyCertAtTime(cert, certificateUsageSSLServer,
      Ci.nsIX509CertDB.FLAG_LOCAL_ONLY, "example.com",
      Math.floor(Date.now() / 1000), () => {
        ok(false, "rejected post-shutdown work must not call back");
      }), /NS_ERROR_NOT_AVAILABLE/, "new verification is rejected after shutdown");
  }
  yield Promise.all(promises);
  equal(completedVerifications, 25, "all pending verifications completed without deadlock");
});
