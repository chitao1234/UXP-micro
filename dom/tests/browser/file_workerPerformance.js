function ok(a, msg) {
  postMessage({type: "status", status: !!a, msg});
}

function is(a, b, msg) {
  ok(a === b, msg);
}

function finish() {
  postMessage({type: "finish"});
}

let isRounded = (x, expectedPrecision) => {
  let rounded = (Math.floor(x / expectedPrecision) * expectedPrecision);
  // First we do the perfectly normal check that should work just fine
  if (rounded === x || x === 0)
    return true;

  // When we're diving by non-whole numbers, we may not get perfect
  // multiplication/division because of floating points
  if (Math.abs(rounded - x + expectedPrecision) < .0000001) {
    return true;
  } else if (Math.abs(rounded - x) < .0000001) {
    return true;
  }

  // Then we handle the case where you're sub-millisecond and the timer is not
  // We check that the timer is not sub-millisecond by assuming it is not if it
  // returns an even number of milliseconds
  if (expectedPrecision < 1 && Math.round(x) == x) {
    if (Math.round(rounded) == x) {
      return true;
    }
  }

  ok(false, "Looming Test Failure, Additional Debugging Info: Expected Precision: " + expectedPrecision + " Measured Value: " + x +
    " Rounded Vaue: " + rounded + " Fuzzy1: " + Math.abs(rounded - x + expectedPrecision) +
    " Fuzzy 2: " + Math.abs(rounded - x));

  return false;
};

function runRTPTests(expectedPrecision) {
  // Try to add some entries.
  performance.mark("Test");
  performance.mark("Test-End");
  performance.measure("Test-Measure", "Test", "Test-End");

  // Check the entries in performance.getEntries/getEntriesByType/getEntriesByName.
  is(performance.getEntries().length, 3, "In a worker, for reduceTimerPrecision: Incorrect number of entries for performance.getEntries() for workers: " + performance.getEntries().length);
  for (var i = 0; i < 3; i++) {
    let startTime = performance.getEntries()[i].startTime;
    let duration = performance.getEntries()[i].duration;
    ok(isRounded(startTime, expectedPrecision), "In a worker, for reduceTimerPrecision(" + expectedPrecision + "), performance.getEntries(" + i.toString() + ").startTime is not rounded: " + startTime.toString());
    ok(isRounded(duration, expectedPrecision), "In a worker, for reduceTimerPrecision(" + expectedPrecision + "), performance.getEntries(" + i.toString() + ").duration is not rounded: " + duration.toString());
  }
  is(performance.getEntriesByType("mark").length, 2, "In a worker, for reduceTimerPrecision: Incorrect number of entries for performance.getEntriesByType() for workers: " + performance.getEntriesByType("resource").length);
  is(performance.getEntriesByName("Test", "mark").length, 1, "In a worker, for reduceTimerPrecision: Incorrect number of entries for performance.getEntriesByName() for workers: " + performance.getEntriesByName("Test", "mark").length);

  finish();
}

function runUserTimingL3Tests() {
  return new Promise((resolve, reject) => {
    performance.clearMarks();
    performance.clearMeasures();
    let observer = new PerformanceObserver(list => {
      try {
        let entries = list.getEntries();
        is(entries.length, 4, "Worker observer receives two marks and two measures");
        for (let entry of entries) {
          is(entry, performance.getEntriesByName(entry.name)[0],
             "Observer and timeline return the same worker entry: " + entry.name);
        }
        let mark = list.getEntriesByName("start")[0];
        let measure = list.getEntriesByName("options")[0];
        is(mark.detail.nested.value, "mark", "Observer retains the cloned mark detail");
        is(measure.detail.nested.value, "measure", "Observer retains cloned measure detail");
        observer.disconnect();
        performance.clearMarks();
        performance.clearMeasures();
        is(performance.getEntries().length, 0, "Worker entries can be cleared after observation");
        resolve();
      } catch (error) {
        observer.disconnect();
        reject(error);
      }
    });
    observer.observe({entryTypes: ["mark", "measure"]});

    let markDetail = {nested: {value: "mark"}, bytes: new Uint8Array([1, 2, 3])};
    markDetail.self = markDetail;
    let start = performance.mark("start", {startTime: 4, detail: markDetail});
    markDetail.nested.value = "changed";
    markDetail.bytes[0] = 99;
    is(start.startTime, 4, "Worker mark honors explicit startTime");
    is(start.detail.nested.value, "mark", "Worker mark detail is cloned");
    is(start.detail.bytes[0], 1, "Worker mark typed-array detail is cloned");
    is(start.detail.self, start.detail, "Worker mark detail preserves cycles");
    performance.mark("end", {startTime: 12});
    let named = performance.measure("named", "start", "end");
    is(named.startTime, 4, "Worker measure resolves a named start mark");
    is(named.duration, 8, "Worker measure resolves a named end mark");

    let measureDetail = {nested: {value: "measure"}, bytes: new Uint8Array([5, 6])};
    let options = performance.measure("options", {
      start: "start", duration: 6, detail: measureDetail
    });
    measureDetail.nested.value = "changed";
    measureDetail.bytes[0] = 99;
    is(options.startTime, 4, "Worker measure options resolve a named start");
    is(options.duration, 6, "Worker measure options honor duration");
    is(options.detail.nested.value, "measure", "Worker measure detail is cloned");
    is(options.detail.bytes[0], 5, "Worker measure typed-array detail is cloned");

    let standalone = new PerformanceMark("standalone", {
      startTime: 2, detail: {value: "constructor"}
    });
    is(standalone.detail.value, "constructor", "PerformanceMark constructor works in a worker");
    is(performance.getEntriesByName("standalone").length, 0,
       "Constructing a mark does not insert it into the timeline");
    for (let create of [
      () => performance.mark("invalid", {detail: function() {}}),
      () => performance.measure("invalid", {start: 0, end: 2, detail: function() {}})
    ]) {
      let exception;
      try { create(); } catch (error) { exception = error; }
      is(exception && exception.name, "DataCloneError", "Worker timing rejects non-cloneable detail");
    }
    is(performance.getEntriesByName("invalid").length, 0,
       "Failed cloning does not insert worker timing entries");
  });
}

self.onmessage = function(e) {
  if (e.data.test == "user-timing-l3") {
    runUserTimingL3Tests().then(() => {
      finish();
      self.close();
    }, error => {
      ok(false, "Worker User Timing L3 failed: " + error + "\n" + error.stack);
      finish();
      self.close();
    });
  } else if (e.data.test == "shutdown") {
    let observer = new PerformanceObserver(() => {});
    observer.observe({entryTypes: ["mark"]});
    performance.mark("pending-at-close", {detail: {value: "shutdown"}});
    ok(true, "Worker can close with a timing observer notification queued");
    finish();
    self.close();
  } else {
    runRTPTests(e.data.precision);
  }
};
