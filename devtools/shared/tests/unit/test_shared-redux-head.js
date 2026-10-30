/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const Services = require("Services");
const defer = require("devtools/shared/defer");

// Load the real cross-harness helper, including its state/action waiters, into
// scopes with the capabilities supplied by browser mochitest or xpcshell.
// Application branding must not choose which harness function is available.
const SHARED_HEAD_URI = Services.io.newFileURI(do_get_file(
  "../../../client/framework/test/shared-redux-head.js")).spec;

function* checkHarnessScope(name, mode) {
  const principal = Cc["@mozilla.org/systemprincipal;1"]
    .createInstance(Ci.nsIPrincipal);
  const scope = new Cu.Sandbox(principal);
  const logs = [];
  scope.Services = { appinfo: { name } };
  scope.defer = defer;
  if (mode !== "xpcshell") {
    scope.info = message => logs.push(["info", message]);
  }
  if (mode !== "browser") {
    scope.do_print = message => logs.push(["do_print", message]);
  }
  if (mode === "non-callable-info") {
    scope.info = "not a browser harness logger";
  }

  try {
    Services.scriptloader.loadSubScript(SHARED_HEAD_URI, scope);
    const sink = mode === "xpcshell" || mode === "non-callable-info" ?
      "do_print" : "info";
    scope.commonLog("direct message");
    equal(logs[0][0], sink, `${name}/${mode} selects the available logger`);
    equal(logs[0][1], "direct message", "The log message is preserved");

    let state = { ready: false };
    const listeners = new Set();
    const store = {
      history: [],
      getState: () => state,
      subscribe(listener) {
        listeners.add(listener);
        return () => listeners.delete(listener);
      }
    };

    const ready = scope.waitUntilState(store, value => value.ready);
    equal(listeners.size, 1, "State waiter subscribes before the state changes");
    state = { ready: true };
    for (const listener of listeners) {
      listener();
    }
    yield ready;
    equal(listeners.size, 0, "State waiter resolves and unsubscribes");

    const action = scope.waitUntilAction(store, "COMPLETE");
    equal(listeners.size, 1, "Action waiter subscribes before dispatch");
    store.history.push({ type: "COMPLETE" });
    for (const listener of listeners) {
      listener();
    }
    equal(yield action, state, "Action waiter resolves with the store state");
    equal(listeners.size, 0, "Action waiter unsubscribes after the matching action");
    equal(logs.length, 5, "Both waiters log their waiting and completion states");
    ok(logs.every(entry => entry[0] === sink),
       "Every helper uses the selected harness logger");
    ok(logs[1][1].startsWith("Waiting for state predicate"), "State wait is logged");
    ok(logs[2][1].startsWith("Found state predicate"), "State completion is logged");
    equal(logs[3][1], 'Waiting for action "COMPLETE"', "Action wait is logged");
    equal(logs[4][1], 'Found action "COMPLETE"', "Action completion is logged");
  } finally {
    Cu.nukeSandbox(scope);
  }
}

add_task(function* () {
  for (const name of ["Basilisk", "Firefox"]) {
    yield* checkHarnessScope(name, "browser");
    yield* checkHarnessScope(name, "xpcshell");
  }
  yield* checkHarnessScope("Basilisk", "both");
  yield* checkHarnessScope("Firefox", "non-callable-info");
});
