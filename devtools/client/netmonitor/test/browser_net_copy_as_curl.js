/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests if Copy as cURL works.
 */

add_task(function* () {
  let { tab, monitor } = yield initNetMonitor(CURL_URL);
  info("Starting test... ");

  // Different quote chars are used for Windows and POSIX
  const QUOTE = Services.appinfo.OS == "WINNT" ? "\"" : "'";

  // Quote a string, escape the quotes inside the string
  function quote(str) {
    return QUOTE + str.replace(new RegExp(QUOTE, "g"), `\\${QUOTE}`) + QUOTE;
  }

  const EXPECTED_HEADERS = [
    "Host: example.com",
    "User-Agent: " + navigator.userAgent,
    "Accept: */*",
    "Accept-Language: " + navigator.language,
    "X-Custom-Header-1: Custom value",
    "X-Custom-Header-2: 8.8.8.8",
    "X-Custom-Header-3: Mon, 3 Mar 2014 11:11:11 GMT",
    "Referer: " + CURL_URL,
    "Connection: keep-alive",
    "Pragma: no-cache",
    "Cache-Control: no-cache"
  ];
  // UXP bd9042426a intentionally omits --compressed; c6a8c16f73 leaves
  // the URL unquoted. c117ee9d29 formats each argument on a continued line.
  // Specify the entire command independently of the production generator.
  const EXPECTED_ARGUMENTS = [SIMPLE_SJS];
  for (let header of EXPECTED_HEADERS) {
    EXPECTED_ARGUMENTS.push("-H", quote(header));
  }
  const CONTINUATION = Services.appinfo.OS == "WINNT" ? " ^\n  " : " \\\n  ";
  const EXPECTED_RESULT = "curl " + EXPECTED_ARGUMENTS.join(CONTINUATION);

  let { NetMonitorView } = monitor.panelWin;
  let { RequestsMenu } = NetMonitorView;
  RequestsMenu.lazyUpdate = false;

  let wait = waitForNetworkEvents(monitor, 1);
  yield ContentTask.spawn(tab.linkedBrowser, SIMPLE_SJS, function* (url) {
    content.wrappedJSObject.performRequest(url);
  });
  yield wait;

  let requestItem = RequestsMenu.getItemAtIndex(0);
  RequestsMenu.selectedItem = requestItem;
  is(requestItem.attachment.method, "GET", "The request uses the default GET method.");
  is(requestItem.attachment.httpVersion, "HTTP/1.1", "The request uses HTTP/1.1.");
  ok(!requestItem.attachment.requestPostData, "The GET request has no payload.");

  yield waitForClipboardPromise(function setup() {
    RequestsMenu.contextMenu.copyAsCurl();
  }, EXPECTED_RESULT);

  info("Clipboard contains a cURL command for the currently selected item's url.");

  yield teardown(monitor);
});
