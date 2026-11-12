/* MPL-2.0. Exercise the actual embedding policy and live overrides. */
const {utils: Cu} = Components;
Cu.import('resource://gre/modules/Services.jsm');

function run_test() {
  const policy = 'javascript.options.mem.shape_lookup_cache';
  const tier = 'browser.memoryTier';
  const prefs = Services.prefs;
  const oldTier = prefs.prefHasUserValue(tier) ? prefs.getIntPref(tier) : null;
  const oldPolicy = prefs.prefHasUserValue(policy) ? prefs.getBoolPref(policy) : null;
  const testing = Cu.getJSTestingFunctions();
  try {
    if (prefs.prefHasUserValue(policy)) prefs.clearUserPref(policy);
    for (const n of [0, 1, 2, 3, 0]) {
      prefs.setIntPref(tier, n);
      equal(testing.shapeLookupCache(), n <= 1, 'tier '+n+' publishes engine policy');
      prefs.setBoolPref(policy, n > 1);
      equal(testing.shapeLookupCache(), n > 1, 'explicit override at tier '+n);
      prefs.clearUserPref(policy);
      equal(testing.shapeLookupCache(), n <= 1, 'clearing override restores tier '+n);
      const sandbox = Cu.Sandbox(Services.scriptSecurityManager.getSystemPrincipal());
      equal(Cu.evalInSandbox('(function(){var o={};for(var i=0;i<80;i++)o["p"+i]=i;'
        +'for(var i=0;i<100;i++){if(o.p20!==20||o.absent!==undefined)throw Error("lookup");}'
        +'delete o.p10;o.p10=100;return o.p10;})()', sandbox),100,'lookup and mutation stay correct');
      Cu.nukeSandbox(sandbox);
    }
  } finally {
    if (oldTier === null) prefs.clearUserPref(tier); else prefs.setIntPref(tier, oldTier);
    if (oldPolicy === null) prefs.clearUserPref(policy); else prefs.setBoolPref(policy, oldPolicy);
  }
}
