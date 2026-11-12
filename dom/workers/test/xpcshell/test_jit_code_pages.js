/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

const {utils: Cu} = Components;
Cu.import('resource://gre/modules/Services.jsm');
do_load_manifest('data/chrome.manifest');

add_task(function* test_process_policy_and_worker_lifetimes() {
  const policy = 'javascript.options.mem.jit_code_decommit';
  const tier = 'browser.memoryTier';
  const prefs = Services.prefs;
  const oldTier = prefs.prefHasUserValue(tier) ? prefs.getIntPref(tier) : null;
  const oldPolicy = prefs.prefHasUserValue(policy) ? prefs.getBoolPref(policy) : null;
  const testing = Cu.getJSTestingFunctions();
  const workers = [];
  const uri = 'chrome://workers/content/worker_jit_code_pages.js';
  function round(worker, id) {
    return new Promise((resolve, reject) => {
      worker.onerror = event => {
        event.preventDefault();
        reject(new Error(event.message));
      };
      worker.onmessage = event => {
        if (event.data.error) {
          reject(new Error(event.data.error + '\n' + event.data.stack));
        } else {
          equal(event.data.round, id, 'worker completed the requested round');
          equal(event.data.calls, 3072, 'worker checked every fresh JIT/IC result');
          resolve(event.data);
        }
      };
      worker.postMessage(id);
    });
  }
  try {
    if (prefs.prefHasUserValue(policy)) prefs.clearUserPref(policy);
    // Low tiers reclaim dead backing; higher tiers retain it. Explicit
    // preference overrides survive transitions in either direction.
    for (const value of [0, 1, 2, 3, 0]) {
      prefs.setIntPref(tier, value);
      const expected = value <= 1;
      equal(testing.jitCodeDecommit(), expected, 'default follows tier ' + value);
      prefs.setBoolPref(policy, true);
      equal(testing.jitCodeDecommit(), true, 'explicit enable at tier ' + value);
      prefs.setBoolPref(policy, false);
      equal(testing.jitCodeDecommit(), false, 'explicit disable at tier ' + value);
      prefs.clearUserPref(policy);
      equal(testing.jitCodeDecommit(), expected, 'clearing override restores tier default');
    }

    workers.push(new Worker(uri), new ChromeWorker(uri));
    // Both pre-existing runtimes retain executable functions across changes.
    // Alternating a tier while explicitly enabled also exercises the distinct
    // tier preference callback without resetting the process-wide override.
    const arms = [false, true, false, true, true, false];
    for (let id = 0; id < arms.length; id++) {
      prefs.setBoolPref(policy, arms[id]);
      prefs.setIntPref(tier, id % 4);
      equal(testing.jitCodeDecommit(), arms[id], 'process policy before worker round');
      const replies = yield Promise.all(workers.map(worker => round(worker, id)));
      for (const reply of replies)
        equal(reply.retained, id + 1, 'old worker functions survive the transition');
      equal(testing.jitCodeDecommit(), arms[id], 'worker startup does not reset policy');
    }

    // New worker runtimes created while enabled use the same process policy;
    // they do not publish a default-off value over an existing main runtime.
    prefs.setBoolPref(policy, true);
    const fresh = new ChromeWorker(uri);
    workers.push(fresh);
    const enabled = yield round(fresh, 6);
    equal(enabled.retained, 1, 'fresh runtime compiled its first functions');
    equal(testing.jitCodeDecommit(), true, 'new runtime retained explicit enable');
    prefs.setIntPref(tier, 3);
    prefs.clearUserPref(policy);
    equal(testing.jitCodeDecommit(), false, 'clearing policy while workers live restores high-tier default');
    const disabled = yield round(fresh, 7);
    equal(disabled.retained, 2, 'fresh runtime code survives disabling backing reclamation');
  } finally {
    for (const worker of workers) worker.terminate();
    if (oldPolicy === null) prefs.clearUserPref(policy); else prefs.setBoolPref(policy, oldPolicy);
    if (oldTier === null) prefs.clearUserPref(tier); else prefs.setIntPref(tier, oldTier);
  }
});
