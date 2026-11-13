/* MPL-2.0. Main-thread preference publication and live policy transitions. */
const {utils: Cu}=Components;
Cu.import('resource://gre/modules/Services.jsm');

function run_test() {
  const policy='javascript.options.mem.lazy_module_exports';
  const prefs=Services.prefs;
  const old=prefs.prefHasUserValue(policy)?prefs.getBoolPref(policy):null;
  const tier='browser.memoryTier';
  const oldTier=prefs.prefHasUserValue(tier)?prefs.getIntPref(tier):null;
  const testing=Cu.getJSTestingFunctions();
  try {
    if(prefs.prefHasUserValue(policy))prefs.clearUserPref(policy);
    for(const value of [3,0,1,2,-1,99,0,3]) {
      prefs.setIntPref(tier,value);
      equal(testing.lazyModuleExports(),value>=0&&value<=1,'tier default '+value);
    }
    prefs.setBoolPref(policy,false);
    prefs.setIntPref(tier,0);
    equal(testing.lazyModuleExports(),false,'explicit off survives low tier');
    prefs.setBoolPref(policy,true);
    prefs.setIntPref(tier,3);
    equal(testing.lazyModuleExports(),true,'explicit on survives high tier');
    prefs.clearUserPref(policy);
    equal(testing.lazyModuleExports(),false,'clearing override restores tier');
    for(const enabled of [true,false,true,false]) {
      prefs.setBoolPref(policy,enabled);
      equal(testing.lazyModuleExports(),enabled,'live engine policy publication');
      const sandbox=Cu.Sandbox(Services.scriptSecurityManager.getSystemPrincipal());
      try {
        equal(Cu.evalInSandbox('class C { x=40; } function later(){return new C().x+2;}later();',sandbox),
              42,'ordinary script behavior is preserved');
      } finally {
        Cu.nukeSandbox(sandbox);
      }
    }
  } finally {
    if(old===null)prefs.clearUserPref(policy);else prefs.setBoolPref(policy,old);
    if(oldTier===null)prefs.clearUserPref(tier);else prefs.setIntPref(tier,oldTier);
  }
}
