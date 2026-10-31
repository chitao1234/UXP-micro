/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { utils: Cu } = Components;
const { Services } = Cu.import("resource://gre/modules/Services.jsm", {});
let manager;
let nextLock = 0;
const ownedLocks = new Set();
const endpoint = {
  assertPermission(permission) {
    throw new Error("Unexpected settings security failure: " + permission);
  }
};

// Use the production lock queue and native IndexedDB. The endpoint only
// identifies this caller; the test consumes task promises instead of IPC replies.
function createLock() {
  const lockID = "settings-transaction-test-" + ++nextLock;
  manager.receiveMessage({
    name: "Settings:CreateLock",
    target: endpoint,
    principal: Services.scriptSecurityManager.getSystemPrincipal(),
    data: { lockID, isServiceLock: false, windowID: 0, lockStack: new Error().stack }
  });
  ownedLocks.add(lockID);
  return lockID;
}

function runTask(lockID, operation, data = {}) {
  const result = manager.queueTask(operation, Object.assign({ lockID }, data));
  manager.startRunning(lockID);
  return result;
}

function finalize(lockID) {
  return runTask(lockID, "finalize");
}

add_task(function* initialize() {
  do_get_profile();
  manager = Cu.import("resource://gre/modules/SettingsRequestManager.jsm", {})
              .SettingsRequestManager;
  do_register_cleanup(() => {
    for (let lockID of ownedLocks) {
      manager.removeLock(lockID);
    }
    manager.settingsDB.close();
  });
  yield manager.ensureConnection();
});

add_task(function* commit_before_unlock_and_queued_read() {
  const values = {
    "test.transactions.bool": false,
    "test.transactions.number": 0,
    "test.transactions.string": "committed",
    "test.transactions.object": { active: true }
  };
  const writer = createLock();
  yield runTask(writer, "set", { settings: values });

  let completeObserved = false;
  const info = manager.lockInfo[writer];
  const getObjectStore = info.getObjectStore;
  info.getObjectStore = function() {
    const store = getObjectStore.call(this);
    store.transaction.addEventListener("complete", () => {
      completeObserved = true;
      ok(writer in manager.lockInfo, "The writer keeps its lock through commit");
    });
    return store;
  };

  const committed = finalize(writer);
  const reader = createLock();
  const read = runTask(reader, "get", { name: "*" });
  const results = yield Promise.all([committed, read]);
  ok(completeObserved, "Finalize waits for the native transaction complete event");
  ok(!(writer in manager.lockInfo), "The committed writer releases its lock");
  for (let name of Object.keys(values)) {
    deepEqual(results[1][name], values[name], "A queued lock reads persisted " + name);
  }
  yield finalize(reader);

  const update = createLock();
  const afterUpdate = createLock();
  const queuedRead = runTask(afterUpdate, "get", { name: "test.transactions.number" });
  yield runTask(update, "set", { settings: { "test.transactions.number": 7 } });
  yield finalize(update);
  const updated = yield queuedRead;
  equal(updated["test.transactions.number"], 7, "Queued locks preserve write/read FIFO order");
  yield finalize(afterUpdate);
});

add_task(function* abort_rejects_and_releases_the_queue() {
  const name = "test.transactions.aborted";
  const writer = createLock();
  yield runTask(writer, "set", { settings: { [name]: "must not persist" } });

  let abortObserved = false;
  let resolveAbort;
  const aborted = new Promise(resolve => { resolveAbort = resolve; });
  let changeObserved = false;
  const observer = {
    observe(subject) {
      if (subject.wrappedJSObject.key === name) {
        changeObserved = true;
      }
    }
  };
  Services.obs.addObserver(observer, "mozsettings-changed", false);
  const info = manager.lockInfo[writer];
  const getObjectStore = info.getObjectStore;
  info.getObjectStore = function() {
    const store = getObjectStore.call(this);
    const transaction = store.transaction;
    transaction.addEventListener("abort", () => {
      abortObserved = true;
      resolveAbort();
    });
    // Abort the real transaction after finalize queues its native requests,
    // before any IndexedDB completion event. No timers or database mocks.
    Promise.resolve().then(() => transaction.abort());
    return store;
  };

  try {
    yield Promise.all([
      finalize(writer).then(
        () => ok(false, "An aborted finalize must reject"),
        error => equal(error, "Set transaction failure", "Finalize reports transaction failure")),
      aborted
    ]);
    ok(abortObserved, "The native transaction abort event was observed");
    ok(!changeObserved, "Aborted writes do not notify settings observers");
    ok(!(writer in manager.lockInfo), "The aborted writer releases its lock");

    const reader = createLock();
    const result = yield runTask(reader, "get", { name });
    ok(!(name in result), "The next lock runs and cannot see the aborted value");
    yield finalize(reader);
  } finally {
    Services.obs.removeObserver(observer, "mozsettings-changed");
  }
});
