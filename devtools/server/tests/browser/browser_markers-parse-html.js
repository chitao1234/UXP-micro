/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Test that we get "Parse HTML" markers.
 */

const { TimelineFront } = require("devtools/shared/fronts/timeline");
const MARKER_NAME = "Parse HTML";

add_task(function* () {
  let browser = yield addTab(MAIN_DOMAIN + "doc_innerHTML.html");
  let doc = browser.contentDocument;

  initDebuggerServer();
  let client = new DebuggerClient(DebuggerServer.connectPipe());
  let form = yield connectDebuggerClient(client);
  let front = TimelineFront(client, form);
  yield front.start({ withMarkers: true, withFrames: true, withGCEvents: true });

  let markers = yield waitForMarkerType(front, MARKER_NAME,
    null, markers => markers, "markers");
  yield front.stop();

  ok(markers.some(m => m.name === MARKER_NAME), `got some ${MARKER_NAME} markers`);

  yield client.close();
  gBrowser.removeCurrentTab();
});
