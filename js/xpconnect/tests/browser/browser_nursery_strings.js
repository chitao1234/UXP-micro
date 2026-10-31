/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Observe the selected embedding policy; never enable it from this test.
// Cu's native APIs exercise the browser runtime, rather than a standalone shell.
add_task(function* test_nursery_strings_embedding() {
  const testing = Cu.getJSTestingFunctions();
  const environment = Cc["@mozilla.org/process/environment;1"].getService(Ci.nsIEnvironment);
  is(typeof testing.nurseryStrings, "function", "Native nursery policy API exists");
  is(typeof testing.isNurseryAllocated, "function", "Native allocation query exists");
  is(typeof testing.minorgc, "function", "Native minor collection API exists");

  function policy(pref, envName) {
    if (Services.prefs.getPrefType(pref) == Ci.nsIPrefBranch.PREF_BOOL) {
      return Services.prefs.getBoolPref(pref);
    }
    // Match the native embedding's environment fallback when a pref is absent.
    return !!parseInt(environment.get(envName), 10);
  }
  let enabled = policy("javascript.options.nursery_strings", "MOZ_NURSERY_STRINGS");
  const configuredDedup = policy("javascript.options.nursery_string_dedup",
                                 "MOZ_NURSERY_STRING_DEDUP");
  let dedup = enabled && configuredDedup;
  let expectedMode = null;
  if (environment.exists("MOZ_TEST_NURSERY_MODE")) {
    expectedMode = environment.get("MOZ_TEST_NURSERY_MODE");
    const validMode = ["off", "on", "dedup"].indexOf(expectedMode) != -1;
    ok(validMode, "Qualification supplies a valid independent expected nursery mode");
    if (!validMode) {
      throw new Error("Invalid MOZ_TEST_NURSERY_MODE: " + expectedMode);
    }
    // Derive every subsequent activation/traffic expectation from the driver,
    // so an earlier test cannot turn an intended on arm into passing off checks.
    const selectedEnabled = enabled;
    enabled = expectedMode != "off";
    dedup = expectedMode == "dedup";
    is(Services.prefs.getPrefType("javascript.options.nursery_strings"),
       Ci.nsIPrefBranch.PREF_BOOL, "Qualification explicitly selects a nursery pref");
    is(Services.prefs.getPrefType("javascript.options.nursery_string_dedup"),
       Ci.nsIPrefBranch.PREF_BOOL, "Qualification explicitly selects a dedup pref");
    is(selectedEnabled, enabled, "Current nursery pref matches the requested qualification arm");
    is(configuredDedup, dedup, "Current dedup pref matches the requested qualification arm");
  }
  is(testing.nurseryStrings().enabled, enabled,
     "Actual browser nursery state matches the selected pref/environment policy");

  // Clear pre-existing young strings so control-arm counter deltas are useful.
  // Promote the owner first, also exercising writes into a tenured container.
  let retained = {values: [], dependents: [], ropes: []};
  testing.gc();
  testing.minorgc();
  const before = testing.nurseryStrings();
  const probe = JSON.parse('"browser-nursery-ordinary-allocation-probe"');
  const probeWasYoung = testing.isNurseryAllocated(probe);
  is(probeWasYoung, enabled, "Fresh ordinary string follows native allocation policy");

  function valueFor(index) {
    return "browser-nursery-retained-string-" +
           (index % 2 ? "\u1234\ud800-two-byte-" : "latin1-") + (index % 17);
  }
  for (let index = 0; index < 128; ++index) {
    const text = valueFor(index);
    // JSON parsing yields fresh ordinary cells, not atoms or shared dependent
    // source slices. Leave this population unexposed until promotion so the
    // dedup arm has genuinely eligible duplicate cells.
    retained.values.push(JSON.parse(JSON.stringify(text)));
    const base = JSON.parse(JSON.stringify(text.repeat(4)));
    retained.dependents.push(base.slice(2, -2));
    retained.ropes.push(JSON.parse(JSON.stringify(text)) + "-rope-suffix");
  }
  const allocated = testing.nurseryStrings();
  testing.minorgc();
  const promoted = testing.nurseryStrings();
  if (enabled) {
    ok(allocated.allocated > before.allocated,
       "Browser allocated real nursery strings");
    ok(promoted.promoted > before.promoted,
       "Retained browser strings were really promoted");
  } else {
    is(allocated.allocated, before.allocated,
       "Off control does not allocate nursery strings");
    is(promoted.promoted, before.promoted,
       "Off control does not promote nursery strings");
  }
  if (dedup) {
    ok(promoted.dedupWork > before.dedupWork,
       "Dedup arm performed native promotion-time comparison work");
    ok(promoted.deduplicated > before.deduplicated,
       "Dedup arm actually shared duplicate retained cells");
  } else {
    is(promoted.dedupWork, before.dedupWork,
       "Off/on controls do not perform deduplication work");
    is(promoted.deduplicated, before.deduplicated,
       "Off/on controls do not deduplicate retained cells");
  }
  is(testing.isNurseryAllocated(probe), false,
     "A retained ordinary probe is tenured after minor collection");

  // Cross-compartment clone storage and wrappers must preserve string values
  // through major collection as well. This follows the normal XPConnect clone
  // path after the activation/dedup population has been measured.
  const sandbox = Cu.Sandbox(Services.scriptSecurityManager.getSystemPrincipal());
  try {
    sandbox.retained = Cu.cloneInto(retained, sandbox);
    testing.gc(null, "shrinking");
    for (let index = 0; index < retained.values.length; ++index) {
      const expected = valueFor(index);
      is(retained.values[index], expected, "Ordinary string survives GC: " + index);
      is(retained.dependents[index], expected.repeat(4).slice(2, -2),
         "Dependent string survives GC: " + index);
      is(retained.ropes[index], expected + "-rope-suffix",
         "Rope string survives GC: " + index);
    }
    is(Cu.evalInSandbox("JSON.stringify(retained)", sandbox), JSON.stringify(retained),
       "Cross-compartment cloned strings survive major GC unchanged");
  } finally {
    Cu.nukeSandbox(sandbox);
  }
  is(testing.nurseryStrings().enabled, enabled,
     "Qualification leaves the browser's allocation policy unchanged");
  info("Browser nursery-string activation: " + JSON.stringify({
    expectedMode, enabled, dedup, before, allocated, promoted, after: testing.nurseryStrings()
  }));
});
