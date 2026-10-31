/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { HttpServer } = Components.utils.import("resource://testing-common/httpd.js", {});
const { AppCacheUtils } = Components.utils.import(
  "resource://devtools/client/shared/AppCacheUtils.jsm", {});

function run_test() {
  do_get_profile();
  run_next_test();
}

add_task(function* () {
  let server = new HttpServer();
  server.registerPathHandler("/headers", (request, response) => {
    response.setStatusLine(request.httpVersion, 200, "OK");
    response.setHeader("Content-Type", "text/plain; charset=utf-8", false);
    response.setHeader("X-AppCache-Response", "response-only", false);
    response.write("manifest response");
  });
  server.start(-1);

  try {
    let host = "localhost:" + server.identity.primaryPort;
    let result = yield new AppCacheUtils()._getURIInfo("http://" + host + "/headers");

    ok(result.success, "The URI request completes successfully");
    equal(result.text, "manifest response", "The response body is preserved");
    equal(result.requestHeaders.host, host, "Request headers are collected separately");
    equal(result.responseHeaders["x-appcache-response"], "response-only",
          "Response headers use lowercase names");
    ok(!("x-appcache-response" in result.requestHeaders),
       "Response headers are not copied into request headers");
    ok(!("host" in result.responseHeaders),
       "Request headers are not copied into response headers");
    for (let headers of [result.requestHeaders, result.responseHeaders]) {
      for (let name of Object.keys(headers)) {
        equal(name, name.toLowerCase(), "Header lookup keys are case insensitive");
      }
    }
  } finally {
    yield new Promise(resolve => server.stop(resolve));
  }
});
