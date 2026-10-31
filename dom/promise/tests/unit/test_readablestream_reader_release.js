/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Browser-embedding regression for Firefox 78's
// js/src/jit-test/tests/stream/reader-closedPromise-handled-2.js. Promise
// identity and rejection expectations follow current WPT's
// streams/resources/rs-test-templates.js. The normal xpcshell rejection
// observer stays enabled; internal stream promises must be marked handled.
add_task(function* test_reader_release_internal_rejections() {
  let samples = [];
  let uncaught = new Set();
  let observer = {
    onLeftUncaught(promise) {
      uncaught.add(PromiseDebugging.getPromiseID(promise));
    },
    onConsumed() {}
  };
  PromiseDebugging.addUncaughtRejectionObserver(observer);
  try {
    for (let byob of [false, true]) {
      for (let state of ["readable", "closed", "errored"]) {
        let label = (byob ? "BYOB " : "default ") + state;
        let controller;
        let source = {start(c) { controller = c; }};
        if (byob) {
          source.type = "bytes";
        }
        let stream = new ReadableStream(source);
        let reader = byob ? stream.getReader({mode: "byob"}) : stream.getReader();
        let before = reader.closed;
        let originalError = new Error(label + " original error");
        if (state == "closed") {
          controller.close();
        } else if (state == "errored") {
          controller.error(originalError);
        }

        Assert.ok(stream.locked, label + ": acquiring the reader locks the stream");
        let releaseResult = reader.releaseLock();
        let after = reader.closed;
        // Retain every promise for cleanup even if an assertion below fails.
        samples.push({label, state, before, after, originalError, releaseResult});
        Assert.ok(!stream.locked, label + ": releasing unlocks the stream");
        Assert.equal(before === after, state == "readable",
                     label + ": the closed promise has the specified identity");
        let result = PromiseDebugging.getState(after);
        Assert.equal(result.state, "rejected", label + ": closed stays rejected");
        Assert.ok(result.reason instanceof TypeError,
                  label + ": release rejects closed with a TypeError");
        reader.releaseLock();
        Assert.equal(reader.closed, after,
                     label + ": releasing twice preserves the rejected promise");
      }
    }

    // Do not attach author rejection handlers until native deferred rejection
    // reporting has had a chance to run. Catching earlier would hide this bug.
    yield new Promise(resolve => do_execute_soon(resolve));
    yield new Promise(resolve => do_execute_soon(resolve));
    let leaked = samples.filter(sample =>
      uncaught.has(PromiseDebugging.getPromiseID(sample.after)));

    let observations = [];
    for (let sample of samples) {
      observations.push(sample.after.then(() => {
        Assert.ok(false, sample.label + ": release must not fulfill closed");
      }, error => {
        Assert.ok(error instanceof TypeError,
                  sample.label + ": the TypeError remains observable to authors");
      }));
      observations.push(sample.before.then(value => {
        Assert.equal(sample.state, "closed",
                     sample.label + ": only the original closed stream fulfills");
        Assert.equal(value, undefined, sample.label + ": fulfillment is undefined");
      }, error => {
        if (sample.state == "errored") {
          Assert.equal(error, sample.originalError,
                       sample.label + ": the original stream error is preserved");
        } else {
          Assert.ok(error instanceof TypeError,
                    sample.label + ": the readable stream's promise rejects");
        }
      }));
    }
    yield Promise.all(observations);
    for (let sample of samples) {
      Assert.equal(sample.releaseResult, undefined,
                   sample.label + ": releaseLock returns undefined");
    }
    Assert.deepEqual(leaked.map(sample => sample.label), [],
                     "Release state promises are internally handled before author observation");
  } finally {
    PromiseDebugging.removeUncaughtRejectionObserver(observer);
    for (let sample of samples) {
      sample.before.catch(() => {});
      sample.after.catch(() => {});
    }
  }
});
