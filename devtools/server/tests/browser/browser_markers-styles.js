/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Test that we get "Styles" markers with correct meta.
 */

const { TimelineFront } = require("devtools/shared/fronts/timeline");
const MARKER_NAME = "Styles";

add_task(function* () {
  let browser = yield addTab(MAIN_DOMAIN + "doc_perf.html");
  let doc = browser.contentDocument;

  initDebuggerServer();
  let client = new DebuggerClient(DebuggerServer.connectPipe());
  let form = yield connectDebuggerClient(client);
  let front = TimelineFront(client, form);
  yield front.start({ withMarkers: true, withFrames: true, withGCEvents: true });

  let markers = yield waitForMarkerType(front, MARKER_NAME, function (markers) {
    return markers.some(({restyleHint}) => restyleHint != void 0);
  }, markers => markers, "markers");

  yield front.stop();

  ok(markers.some(m => m.name === MARKER_NAME), `got some ${MARKER_NAME} markers`);
  ok(markers.some(({restyleHint}) => restyleHint != void 0),
    "Some markers have a restyleHint.");

  yield client.close();
  gBrowser.removeCurrentTab();
});
