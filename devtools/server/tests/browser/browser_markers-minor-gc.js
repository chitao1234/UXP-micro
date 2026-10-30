/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Test that we get "MinorGC" markers when we continue to steadily allocate
 * objects.
 */

const { TimelineFront } = require("devtools/shared/fronts/timeline");

add_task(function* () {
  // This test runs very slowly on linux32 debug EC2 instances.
  requestLongerTimeout(2);

  let doc = yield addTab(MAIN_DOMAIN + "doc_allocations.html");

  initDebuggerServer();
  let client = new DebuggerClient(DebuggerServer.connectPipe());
  let form = yield connectDebuggerClient(client);
  let front = TimelineFront(client, form);
  yield front.start({ withMarkers: true, withFrames: true, withGCEvents: true });

  let markers = yield waitForMarkerType(front, ["MinorGC"],
    null, markers => markers, "markers");
  yield front.stop();

  ok(markers.some(m => m.name === "MinorGC" && m.causeName),
     "got some MinorGC markers");

  yield client.close();
  gBrowser.removeCurrentTab();
});
