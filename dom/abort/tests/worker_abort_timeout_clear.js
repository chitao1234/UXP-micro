/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Isolate this timer-ID sweep from the parent's test watchdog and harness.
// State/exception checks follow current Firefox's dom/abort/timeout.any.js.
const signal = AbortSignal.timeout(10);
const isSignal = signal instanceof AbortSignal;
const initiallyAborted = signal.aborted;
signal.onabort = () => postMessage({
  isSignal,
  initiallyAborted,
  aborted: signal.aborted,
  isDOMException: signal.reason instanceof DOMException,
  reasonName: signal.reason.name,
});
for (let n = 1; n <= 1000; ++n) clearTimeout(n);
