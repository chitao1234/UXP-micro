// -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
"use strict";

// In which we connect to a domain (as faked by a server running locally)
// and start up an OCSP responder (also basically faked) that gives a
// response with a bad signature. With security.OCSP.require set to true,
// this should fail (but it also shouldn't cause assertion failures).

var gOCSPRequestCount = 0;

function run_test() {
  do_get_profile();
  Services.prefs.setBoolPref("security.OCSP.require", true);
  Services.prefs.setIntPref("security.OCSP.enabled", 1);
  Services.prefs.setIntPref("security.tls.version.min", 3);
  Services.prefs.setIntPref("security.tls.version.max", 4);
  do_register_cleanup(() => {
    Services.prefs.clearUserPref("security.tls.version.min");
    Services.prefs.clearUserPref("security.tls.version.max");
  });

  // We don't actually make use of stapling in this test. This is just how we
  // get a TLS connection.
  add_tls_server_setup("OCSPStaplingServer", "ocsp_certs");

  let args = [["bad-signature", "default-ee", "unused"]];
  let ocspResponses = generateOCSPResponses(args, "ocsp_certs");
  let ocspResponseBadSignature = ocspResponses[0];

  let ocspResponder = new HttpServer();
  ocspResponder.registerPrefixHandler("/", function (request, response) {
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.setHeader("Content-Type", "application/ocsp-response");
    response.write(ocspResponseBadSignature);
    gOCSPRequestCount++;
  });
  ocspResponder.start(8888);

  add_tests();

  add_test(function () { ocspResponder.stop(run_next_test); });

  run_next_test();
}

function add_tests()
{
  function check_tls_version(securityInfo) {
    let socket = securityInfo.QueryInterface(Ci.nsISSLSocketControl);
    equal(socket.SSLVersionOffered, Ci.nsISSLSocketControl.TLS_VERSION_1_3,
          "OCSP signature failures must not cause TLS version fallback");
  }
  // Both connections must fail for the OCSP signature, not a subsequent
  // downgrade-protection error after the first failure changed cached state.
  add_connection_test("ocsp-stapling-none.example.com",
                      SEC_ERROR_OCSP_BAD_SIGNATURE, null, check_tls_version);
  add_connection_test("ocsp-stapling-none.example.com",
                      SEC_ERROR_OCSP_BAD_SIGNATURE, null, check_tls_version);
  add_test(function () {
    equal(gOCSPRequestCount, 1,
          "OCSP request count should be 1 due to OCSP response caching");
    gOCSPRequestCount = 0;
    run_next_test();
  });
}
