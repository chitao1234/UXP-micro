/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

// Preserve dynamic private-state coverage independently of the static preload
// table removed by UXP 595c8d53df. No preloaded host or table lifetime is used.
do_get_profile();
const sss = Cc["@mozilla.org/ssservice;1"].getService(Ci.nsISiteSecurityService);
const PRIVATE = Ci.nsISocketProvider.NO_PERMANENT_STORAGE;
const HOST = "dynamic-private.example.com";
const STATUS = new FakeSSLStatus();

function processHeader(host, header, flags) {
  sss.processHeader(Ci.nsISiteSecurityService.HEADER_HSTS,
    Services.io.newURI("https://" + host, null, null), header, STATUS, flags);
}

function secure(host, flags) {
  return sss.isSecureHost(Ci.nsISiteSecurityService.HEADER_HSTS, host, flags);
}

do_register_cleanup(() => sss.clearAll());

add_task(function* test_private_dynamic_hsts_isolation() {
  sss.clearAll();
  for (let flags of [0, PRIVATE]) {
    ok(!secure(HOST, flags), "fresh dynamic state is empty for flags " + flags);
    processHeader(HOST, "max-age=1000; includeSubDomains", flags);
    ok(secure(HOST, flags), "a header enables dynamic HSTS for flags " + flags);
    ok(secure("sub." + HOST, flags), "includeSubDomains applies for flags " + flags);
  }

  processHeader(HOST, "max-age=0", PRIVATE);
  ok(!secure(HOST, PRIVATE), "private max-age=0 removes private state");
  ok(!secure("sub." + HOST, PRIVATE), "private removal drops inherited protection");
  ok(secure(HOST, 0), "private removal preserves persistent state");
  ok(secure("sub." + HOST, 0), "private removal preserves persistent subdomains");

  processHeader(HOST, "max-age=1000", PRIVATE);
  ok(secure(HOST, PRIVATE), "private policy can be restored");
  ok(!secure("sub." + HOST, PRIVATE), "omitted includeSubDomains stays omitted");

  Services.obs.notifyObservers(null, "last-pb-context-exited", null);
  ok(!secure(HOST, PRIVATE), "leaving private browsing clears dynamic private state");
  ok(secure(HOST, 0), "leaving private browsing preserves persistent state");
  ok(secure("sub." + HOST, 0), "persistent includeSubDomains remains intact");
});

add_task(function* test_expiration_and_removal() {
  const expiring = "expiring-private.example.net";
  for (let flags of [0, PRIVATE]) {
    processHeader(expiring, "max-age=1; includeSubDomains", flags);
    ok(secure(expiring, flags), "unexpired dynamic state applies for flags " + flags);
    ok(secure("sub." + expiring, flags), "unexpired inherited state applies");
  }
  yield new Promise(resolve => do_timeout(1250, resolve));
  for (let flags of [0, PRIVATE]) {
    ok(!secure(expiring, flags), "expired dynamic state is removed for flags " + flags);
    ok(!secure("sub." + expiring, flags), "expired state no longer protects subdomains");
  }
  sss.clearAll();
  ok(!secure(HOST, 0), "clearAll removes persistent dynamic state");
  ok(!secure(HOST, PRIVATE), "clearAll removes private dynamic state");
});
