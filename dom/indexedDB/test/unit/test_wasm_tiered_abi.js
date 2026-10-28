/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

var testGenerator = testSteps();

function testSteps()
{
  const { Services } = Components.utils.import("resource://gre/modules/Services.jsm", {});
  const name = "test_wasm_tiered_abi.js";
  const objectStoreName = "Wasm";
  const prefs = Services.prefs;
  const tierPref = "browser.memoryTier";
  const sharedPref = "javascript.options.wasm_shared_code";
  const hadTierPref = prefs.prefHasUserValue(tierPref);
  const hadSharedPref = prefs.prefHasUserValue(sharedPref);
  const oldTier = prefs.getIntPref(tierPref, 3);
  const oldShared = prefs.getBoolPref(sharedPref, false);

  function restorePrefs()
  {
    if (hadTierPref)
      prefs.setIntPref(tierPref, oldTier);
    else
      prefs.clearUserPref(tierPref);
    if (hadSharedPref)
      prefs.setBoolPref(sharedPref, oldShared);
    else
      prefs.clearUserPref(sharedPref);
  }

  function sharedCode(module)
  {
    return Cu.getJSTestingFunctions().wasmExtractCode(module).sharedCode;
  }

  if (!isWasmSupported()) {
    restorePrefs();
    finishTest();
    yield undefined;
  }

  clearAllDatabases(continueToNextStepSync);
  yield undefined;

  prefs.setIntPref(tierPref, 0);
  prefs.setBoolPref(sharedPref, true);
  getWasmBinary(`(module
    (memory 1 8)
    (global $state (mut i32) (i32.const 42))
    (func $run (result i32) (get_global $state))
    (func $set (param i32) (set_global $state (get_local 0))
      (i32.store (i32.const 0) (get_local 0)))
    (func $load (result i32) (i32.load (i32.const 0)))
    (export "run" $run) (export "set" $set) (export "load" $load)
    (export "memory" memory))`);
  let binary = yield undefined;
  let original = getWasmModule(binary);
  is(sharedCode(original), true, "module compiled under tier 0 records shared mode");

  function checkInstanceState(module, label)
  {
    let first = new WebAssembly.Instance(module).exports;
    let second = new WebAssembly.Instance(module).exports;
    is(first.run(), 42, label + ": initial global");
    first.set(71);
    second.set(93);
    is(first.run(), 71, label + ": first private global");
    is(second.run(), 93, label + ": second private global");
    is(first.load(), 71, label + ": first private memory");
    is(second.load(), 93, label + ": second private memory");
    is(first.memory.grow(1), 1, label + ": grow succeeds");
    is(first.load(), 71, label + ": memory survives growth");
    is(second.load(), 93, label + ": other instance survives growth");
  }

  checkInstanceState(original, "original shared module");

  prefs.setBoolPref(sharedPref, false);
  let optedOut = getWasmModule(binary);
  is(sharedCode(optedOut), false, "tier 0 preference can select classic mode");
  is(sharedCode(original), true, "preference changes do not mutate compiled modules");

  let request = indexedDB.open(name, 1);
  request.onerror = errorHandler;
  request.onupgradeneeded = function(event) {
    event.target.result.createObjectStore(objectStoreName);
  };
  request.onsuccess = continueToNextStepSync;
  yield undefined;

  let db = request.result;
  db.onerror = errorHandler;
  let transaction = db.transaction([objectStoreName], "readwrite");
  transaction.oncomplete = continueToNextStepSync;
  transaction.onerror = errorHandler;
  transaction.objectStore(objectStoreName).put(original, 1);
  yield undefined;

  // Same-mode restore must re-link the cached template, including the
  // instance globals and memory operands, without carrying any live address.
  prefs.setBoolPref(sharedPref, true);
  let sameMode = null;
  request = db.transaction([objectStoreName]).objectStore(objectStoreName).get(1);
  request.onerror = errorHandler;
  request.onsuccess = function() {
    sameMode = request.result;
    continueToNextStepSync();
  };
  yield undefined;
  is(sharedCode(sameMode), true, "same-mode cache remains shared");
  checkInstanceState(sameMode, "restored shared cache");

  // The persisted shared artifact must be rejected at tier 3 and rebuilt from
  // bytecode using the classic policy captured by this request.
  prefs.setIntPref(tierPref, 3);
  prefs.setBoolPref(sharedPref, true);
  let tier3Module = getWasmModule(binary);
  is(sharedCode(tier3Module), false, "tier 3 forces classic even when enabled");
  is(sharedCode(original), true, "tier change does not mutate existing modules");
  let readback = null;
  request = db.transaction([objectStoreName]).objectStore(objectStoreName).get(1);
  request.onerror = errorHandler;
  request.onsuccess = function() {
    readback = request.result;
    continueToNextStepSync();
  };
  yield undefined;
  is(sharedCode(readback), false, "tier change recompiles the cached module as classic");
  is(new WebAssembly.Instance(readback).exports.run(), 42,
     "classic module restored from bytecode remains executable");

  checkInstanceState(readback, "fallback classic module");
  transaction = db.transaction([objectStoreName], "readwrite");
  transaction.oncomplete = continueToNextStepSync;
  transaction.onerror = errorHandler;
  transaction.objectStore(objectStoreName).put(readback, 2);
  yield undefined;
  request = db.transaction([objectStoreName]).objectStore(objectStoreName).get(2);
  request.onerror = errorHandler;
  request.onsuccess = function() {
    sameMode = request.result;
    continueToNextStepSync();
  };
  yield undefined;
  is(sharedCode(sameMode), false, "same-mode cache remains classic");
  checkInstanceState(sameMode, "restored classic cache");

  // Exercise the reverse transition against the newly written classic cache.
  prefs.setIntPref(tierPref, 0);
  request = db.transaction([objectStoreName]).objectStore(objectStoreName).get(2);
  request.onerror = errorHandler;
  request.onsuccess = function() {
    readback = request.result;
    continueToNextStepSync();
  };
  yield undefined;
  is(sharedCode(readback), true, "reverse tier change recompiles classic cache as shared");
  is(new WebAssembly.Instance(readback).exports.run(), 42,
     "shared module restored from bytecode remains executable");

  checkInstanceState(readback, "reverse fallback shared module");

  prefs.setIntPref(tierPref, 1);
  let tier1Module = getWasmModule(binary);
  is(sharedCode(tier1Module), true, "tier 1 selects shared mode when enabled");

  db.close();
  restorePrefs();
  finishTest();
  yield undefined;
}
