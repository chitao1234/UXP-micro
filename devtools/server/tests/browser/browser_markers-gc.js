/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Test that we get "GarbageCollection" markers.
 */

const { TimelineFront } = require("devtools/shared/fronts/timeline");
const { PerformanceRecordingFront } = require("devtools/shared/fronts/performance-recording");
const MARKER_NAME = "GarbageCollection";

add_task(function* () {
  let browser = yield addTab(MAIN_DOMAIN + "doc_force_gc.html");
  let doc = browser.contentDocument;

  initDebuggerServer();
  let client = new DebuggerClient(DebuggerServer.connectPipe());
  let form = yield connectDebuggerClient(client);
  let front = TimelineFront(client, form);
  let markerBatches = [];
  let collectMarkers = markers => markerBatches.push(markers);
  front.on("markers", collectMarkers);
  let onMarkers = waitForMarkerType(front, MARKER_NAME,
    null, markers => markers, "markers");
  let startTime = yield front.start({
    withMarkers: true,
    withFrames: true,
    withGCEvents: true
  });

  let markers = yield onMarkers;
  yield front.stop();
  front.off("markers", collectMarkers);

  ok(markers.some(m => m.name === MARKER_NAME), `got some ${MARKER_NAME} markers`);
  ok(markers.every(({causeName}) => typeof causeName === "string"),
    "All markers have a causeName.");
  ok(markers.every(({cycle}) => typeof cycle === "number"),
    "All markers have a `cycle` ID.");

  // GC and docshell marker batches can arrive out of chronological order.
  // Preserve coverage of the recording model's completion-time sorting without
  // requiring a live SPS profiler or sorting the raw Timeline data in the test.
  let rec = new PerformanceRecordingFront();
  let recordingForm = {
    configuration: { withMarkers: true },
    startTime,
    recording: true,
    completed: false
  };
  rec.form(recordingForm);
  for (let batch of markerBatches) {
    rec._addTimelineData("markers", { markers: batch });
  }
  rec.form(Object.assign({}, recordingForm, {
    recording: false,
    completed: true
  }));
  markers = rec.getMarkers();

  // Bug 1197646
  let ordered = true;
  markers.reduce((previousStart, current, i) => {
    if (i === 0) {
      return current.start;
    }
    if (current.start < previousStart) {
      ok(false, `markers must be in order. ${current.name} marker has later start time (${current.start}) thanprevious: ${previousStart}`);
      ordered = false;
    }
    return current.start;
  });

  is(ordered, true, "All GC and non-GC markers are in order by start time.");

  yield client.close();
  gBrowser.removeCurrentTab();
});
