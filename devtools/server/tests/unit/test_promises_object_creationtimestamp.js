/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Test that we get the approximate time range for promise creation timestamp.
 */

"use strict";

const { PromisesFront } = require("devtools/shared/fronts/promises");

var events = require("sdk/event/core");

// NowAsMillis() in jsdate.cpp floors Date.now() to this interval.
const DATE_NOW_RESOLUTION_MS = 2;

add_task(function* () {
  let client = yield startTestDebuggerServer("promises-object-test");
  let chromeActors = yield getChromeActors(client);

  ok(Promise.toString().includes("native code"), "Expect native DOM Promise.");

  // We have to attach the chrome TabActor before playing with the PromiseActor
  yield attachTab(client, chromeActors);
  yield testPromiseCreationTimestamp(client, chromeActors, v => {
    return new Promise(resolve => resolve(v));
  });

  let response = yield listTabs(client);
  let targetTab = findTab(response.tabs, "promises-object-test");
  ok(targetTab, "Found our target tab.");

  yield testPromiseCreationTimestamp(client, targetTab, v => {
    const debuggee = DebuggerServer.getTestGlobal("promises-object-test");
    return debuggee.Promise.resolve(v);
  });

  yield close(client);
});

function* testPromiseCreationTimestamp(client, form, makePromise) {
  let front = PromisesFront(client, form);
  let resolution = "MyLittleSecret" + Math.random();

  yield front.attach();
  yield front.listPromises();

  let onNewPromise = new Promise(resolve => {
    events.on(front, "new-promises", promises => {
      for (let p of promises) {
        if (p.promiseState.state === "fulfilled" &&
            p.promiseState.value === resolution) {
          resolve(p);
        }
      }
    });
  });

  let monotonicStart = Cu.now();
  let start = Date.now();
  let promise = makePromise(resolution);
  let end = Date.now();

  let grip = yield onNewPromise;
  let maxSamplingDelay = Cu.now() - monotonicStart;
  ok(grip, "Found our new promise.");

  let creationTimestamp = grip.promiseState.creationTimestamp;

  equal(typeof creationTimestamp, "number", "The creation timestamp is numeric.");
  ok(Number.isFinite(creationTimestamp), "The creation timestamp is finite.");

  // The actor subtracts an unrounded promiseLifetime from rounded Date.now().
  // Rounding can move the result backwards by less than 2ms. JavaScript reads
  // Date.now() before promiseLifetime, so their gap can only move it further
  // backwards. Both reads occur after monotonicStart and before receipt, so
  // their gap is bounded by maxSamplingDelay. Cu.now() and promiseLifetime use
  // the same native clock.
  // Use that measured conservative bound instead of assuming a maximum pause.
  let lowerBound = start - DATE_NOW_RESOLUTION_MS - maxSamplingDelay;
  let upperBound = end + DATE_NOW_RESOLUTION_MS;
  ok(lowerBound <= creationTimestamp && creationTimestamp <= upperBound,
    "Expect promise creation timestamp to be within elapsed time range: " +
    lowerBound + " <= " + creationTimestamp + " <= " + upperBound +
    " (maximum sampling delay " + maxSamplingDelay + "ms)");

  yield front.detach();
  // Appease eslint
  void promise;
}
