"use strict";

function run_test() {
  do_get_profile();
  let SSService = Cc["@mozilla.org/ssservice;1"]
                    .getService(Ci.nsISiteSecurityService);

  // check that a host on the preload list is identified as an sts host
  ok(SSService.isSecureHost(Ci.nsISiteSecurityService.HEADER_HSTS,
                            "includesubdomains.preloaded.test", 0));

  // Check static lookup across tiers without changing its security policy.
  for (let tier of [0, 1, 3]) {
    Services.prefs.setIntPref("browser.memoryTier", tier);
    ok(SSService.isSecureHost(Ci.nsISiteSecurityService.HEADER_HSTS,
                             "includesubdomains.preloaded.test", 0));
  }
  Services.prefs.clearUserPref("browser.memoryTier");
  // now simulate that it's 19 weeks later than it actually is
  let offsetSeconds = 19 * 7 * 24 * 60 * 60;
  Services.prefs.setIntPref("test.currentTimeOffsetSeconds", offsetSeconds);

  // check that the preloaded host is no longer considered sts
  ok(!SSService.isSecureHost(Ci.nsISiteSecurityService.HEADER_HSTS,
                             "includesubdomains.preloaded.test", 0));

  // just make sure we can get everything back to normal
  Services.prefs.clearUserPref("test.currentTimeOffsetSeconds");
  ok(SSService.isSecureHost(Ci.nsISiteSecurityService.HEADER_HSTS,
                            "includesubdomains.preloaded.test", 0));
}
