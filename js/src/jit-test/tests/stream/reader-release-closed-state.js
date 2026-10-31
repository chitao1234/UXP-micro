// |jit-test| --enable-streams
// Supplemental coverage for Firefox 78's reader-closedPromise-handled-2.js.
// Promise identity and rejection semantics follow current WPT's
// streams/resources/rs-test-templates.js: templatedRSReader,
// templatedRSClosedReader, and templatedRSErroredReader.

let tracked = new Map();
let notifications = [];
setPromiseRejectionTrackerCallback((promise, state) => {
  tracked.set(promise, state);
  notifications.push([promise, state]);
});

function checkRelease(state, byob) {
  let controller;
  let source = {start(c) { controller = c; }};
  if (byob) {
    source.type = "bytes";
  }
  let stream = new ReadableStream(source);
  let reader = byob ? stream.getReader({mode: "byob"}) : stream.getReader();
  let before = reader.closed;
  let originalError = new Error("original stream error");
  if (state == "closed") {
    controller.close();
  } else if (state == "errored") {
    controller.error(originalError);
  }

  assertEq(stream.locked, true);
  assertEq(reader.releaseLock(), undefined);
  assertEq(stream.locked, false);
  let after = reader.closed;
  assertEq(before === after, state == "readable");
  assertEq(after === reader.closed, true);
  assertEq(reader.releaseLock(), undefined);
  assertEq(reader.closed, after);

  // Inspect tracking before installing author handlers, which must not hide
  // a missing internal PromiseIsHandled step.
  let status = tracked.get(after);
  assertEq(status === 1 || status === undefined, true);

  let releaseError;
  after.catch(error => { releaseError = error; });
  let originalOutcome;
  before.then(value => { originalOutcome = value; },
              error => { originalOutcome = error; });
  drainJobQueue();
  assertEq(releaseError instanceof TypeError, true);
  if (state == "readable") {
    assertEq(originalOutcome, releaseError);
  } else if (state == "closed") {
    assertEq(originalOutcome, undefined);
  } else {
    assertEq(originalOutcome, originalError);
  }
}

for (let byob of [false, true]) {
  for (let state of ["readable", "closed", "errored"]) {
    checkRelease(state, byob);
  }
}

// An author-handled pending closed promise must not receive a redundant
// rejection-tracker notification when release rejects it.
let reader = new ReadableStream().getReader();
let closed = reader.closed;
let observed;
closed.catch(error => { observed = error; });
let notificationCount = notifications.length;
reader.releaseLock();
assertEq(notifications.length, notificationCount);
drainJobQueue();
assertEq(observed instanceof TypeError, true);

// Handling the internal state promise must not suppress unrelated rejections.
let sentinel = {};
let rejected = Promise.reject(sentinel);
assertEq(tracked.get(rejected), 0);
let caught;
rejected.catch(error => { caught = error; });
assertEq(tracked.get(rejected), 1);
drainJobQueue();
assertEq(caught, sentinel);
