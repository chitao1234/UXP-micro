/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if copying a request's request/response headers works.
 */

add_task(function* () {
  let { tab, monitor } = yield initNetMonitor(SIMPLE_URL);
  info("Starting test... ");

  let { NetMonitorView } = monitor.panelWin;
  let { RequestsMenu } = NetMonitorView;

  RequestsMenu.lazyUpdate = false;

  let wait = waitForNetworkEvents(monitor, 1);
  tab.linkedBrowser.reload();
  yield wait;

  let requestItem = RequestsMenu.getItemAtIndex(0);
  RequestsMenu.selectedItem = requestItem;

  let { method, httpVersion, status, statusText } = requestItem.attachment;
  is(method, "GET", "The fixture was requested with GET.");
  is(httpVersion, "HTTP/1.1", "The fixture uses HTTP/1.1.");
  is(status, 200, "The fixture response succeeded.");
  is(statusText, "OK", "The fixture response has the expected status text.");

  // These headers follow the configured network feature set, independently
  // of the actor's raw-header blob that the copy command consumes.
  const EXPECTED_REQUEST_HEADERS = [
    `${method} ${SIMPLE_URL} ${httpVersion}`,
    "Host: example.com",
    "User-Agent: " + navigator.userAgent + "",
    "Accept: " + Services.prefs.getCharPref("network.http.accept.navigation"),
    "Accept-Language: " + navigator.languages.join(",") + ";q=0.5",
    "Accept-Encoding: " + Services.prefs.getCharPref("network.http.accept-encoding"),
    "Connection: keep-alive",
    "Upgrade-Insecure-Requests: 1",
    "Pragma: no-cache",
    "Cache-Control: no-cache"
  ].join("\n");

  yield waitForClipboardPromise(function setup() {
    RequestsMenu.contextMenu.copyRequestHeaders();
  }, function validate(result) {
    // Sometimes, a "Cookie" header is left over from other tests. Remove it:
    result = String(result).replace(/Cookie: [^\n]+\n/, "");
    return result === EXPECTED_REQUEST_HEADERS;
  });
  info("Clipboard contains the currently selected item's request headers.");

  const EXPECTED_RESPONSE_HEADERS = [
    `${httpVersion} ${status} ${statusText}`,
    "last-modified: Sun, 3 May 2015 11:11:11 GMT",
    "content-type: text/html",
    "content-length: 465",
    "connection: close",
    "server: httpd.js",
    "date: Sun, 3 May 2015 11:11:11 GMT"
  ].join("\n");
  // text/unicode reads convert native CRLF to DOM LF on Windows. Keep exact
  // comparisons so missing/extra fields and incorrect line breaks still fail.

  yield waitForClipboardPromise(function setup() {
    RequestsMenu.contextMenu.copyResponseHeaders();
  }, function validate(result) {
    // Fake the "Last-Modified" and "Date" headers because they will vary:
    result = String(result)
      .replace(/last-modified: [^\n]+ GMT/, "last-modified: Sun, 3 May 2015 11:11:11 GMT")
      .replace(/date: [^\n]+ GMT/, "date: Sun, 3 May 2015 11:11:11 GMT");
    return result === EXPECTED_RESPONSE_HEADERS;
  });
  info("Clipboard contains the currently selected item's response headers.");

  yield teardown(monitor);
});
