/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

// Forget About This Site still removes dynamic HSTS. UXP 55aa6ce7b3 removed
// HPKP, so this test needs neither pinning certificates nor removed pin APIs.
const { ForgetAboutSite } =
  Cu.import("resource://gre/modules/ForgetAboutSite.jsm", {});

do_get_profile();
const sss = Cc["@mozilla.org/ssservice;1"].getService(Ci.nsISiteSecurityService);
const host = "forget-security.example.com";
const unrelatedHost = "keep-security.example.net";
const sslStatus = new FakeSSLStatus();

do_register_cleanup(() => sss.clearAll());

add_task(function* test_forget_dynamic_hsts() {
  sss.clearAll();
  for (let name of [host, unrelatedHost]) {
    sss.processHeader(Ci.nsISiteSecurityService.HEADER_HSTS,
      Services.io.newURI("https://" + name, null, null),
      "max-age=69403; includeSubDomains", sslStatus, 0);
    ok(sss.isSecureHost(Ci.nsISiteSecurityService.HEADER_HSTS, name, 0),
       name + " has dynamic HSTS before forgetting");
    ok(sss.isSecureHost(Ci.nsISiteSecurityService.HEADER_HSTS, "sub." + name, 0),
       name + " protects subdomains before forgetting");
  }

  yield ForgetAboutSite.removeDataFromDomain(host);

  ok(!sss.isSecureHost(Ci.nsISiteSecurityService.HEADER_HSTS, host, 0),
     "forgetting removes the exact host's dynamic HSTS");
  ok(!sss.isSecureHost(Ci.nsISiteSecurityService.HEADER_HSTS, "sub." + host, 0),
     "subdomains no longer inherit the forgotten host's policy");
  ok(sss.isSecureHost(Ci.nsISiteSecurityService.HEADER_HSTS, unrelatedHost, 0),
     "forgetting preserves an unrelated host's policy");
  ok(sss.isSecureHost(Ci.nsISiteSecurityService.HEADER_HSTS,
                      "sub." + unrelatedHost, 0),
     "forgetting preserves unrelated includeSubDomains");
});
