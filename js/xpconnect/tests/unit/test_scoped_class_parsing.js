/* MPL-2.0. Scope full-parser fallback without changing class semantics. */
const {utils: Cu} = Components;
Cu.import('resource://gre/modules/Services.jsm');

function run_test() {
  const pref = 'javascript.options.mem.scoped_class_parsing';
  const prefs = Services.prefs;
  const prior = prefs.prefHasUserValue(pref) ? prefs.getBoolPref(pref) : null;
  const tier = 'browser.memoryTier';
  const priorTier = prefs.prefHasUserValue(tier) ? prefs.getIntPref(tier) : null;
  const testing = Cu.getJSTestingFunctions();
  const principal = Services.scriptSecurityManager.getSystemPrincipal();
  const cases = [
    ['field', 'class Modern { x=42; }'],
    ['static-field', 'class Modern { static x=42; }'],
    ['static-block', 'class Modern { static { this.x=42; } }'],
    ['nested-class', 'class Modern { x=class Nested { y=21; }; }'],
    ['nested-function', 'function outer(){ class Inner { x=42; } return new Inner().x; }'],
    ['computed-key', 'let key="x"; class Modern { [key]=42; }'],
    ['heritage', 'class Base { x=21; } class Modern extends Base { y=21; }'],
  ];
  try {
    if (prefs.prefHasUserValue(pref)) prefs.clearUserPref(pref);
    for (const value of [3,0,1,2,-1,99,0,3]) {
      prefs.setIntPref(tier,value);
      equal(testing.scopedClassParsing(),value>=0&&value<=1,'tier default '+value);
    }
    prefs.setBoolPref(pref,false);
    prefs.setIntPref(tier,0);
    equal(testing.scopedClassParsing(),false,'explicit off survives low tier');
    prefs.setBoolPref(pref,true);
    prefs.setIntPref(tier,3);
    equal(testing.scopedClassParsing(),true,'explicit on survives high tier');
    prefs.clearUserPref(pref);
    equal(testing.scopedClassParsing(),false,'clearing override restores tier');
    for (const enabled of [false, true, false, true]) {
      prefs.setBoolPref(pref, enabled);
      equal(testing.scopedClassParsing(), enabled, 'live embedding policy');
      for (const [name, body] of cases) {
        const sandbox = Cu.Sandbox(principal);
        try {
          const result = JSON.parse(Cu.evalInSandbox(
            'function before(){return 21;} '+body+
            ' function after(x){return x+before();} '+
            'var t=Components.utils.getJSTestingFunctions();'+
            'JSON.stringify({before:t.isLazyFunction(before),after:t.isLazyFunction(after)});', sandbox));
          ok(result.before, name+' keeps the preceding function lazy');
          equal(result.after, enabled, name+' scopes fallback for following function');
          equal(Cu.evalInSandbox('after(21)', sandbox), 42, name+' compiles on first use');
          equal(Cu.evalInSandbox('after(22)', sandbox), 43, name+' remains callable');
        } finally {
          Cu.nukeSandbox(sandbox);
        }
      }
      const sandbox = Cu.Sandbox(principal);
      try {
        equal(Cu.evalInSandbox(
          'let key="field",order=[];class B { get base(){return 7;} }'+
          'class C extends B { x=(order.push(1),2); [key]=()=>this.x;'+
          'static y=(order.push(0),3);static { order.push(4);this.y+=1; }'+
          'm(){return super.base+this.x;} }'+
          'function following(v){return ()=>new C().m()+v+C.y;}'+
          'var f=following(29);var result=f();'+
          'if(order.join(",")!=="0,4,1")throw Error("initializer order");result;', sandbox),
          42, 'closures, super, this, keys and initializer order');
        for (const source of [
          'class Bad { x = ; } function following(){}',
          'class Bad { static { return; } } function following(){}',
          'class Bad { constructor=1; } function following(){}',
          'class Bad { static prototype=1; } function following(){}',
        ]) {
          Assert.throws(() => Cu.evalInSandbox(source, sandbox), /SyntaxError/,
                        'invalid class rejected during initial parse');
        }
      } finally {
        Cu.nukeSandbox(sandbox);
      }
    }
  } finally {
    if (prior === null) prefs.clearUserPref(pref);
    else prefs.setBoolPref(pref, prior);
    if (priorTier === null) prefs.clearUserPref(tier);
    else prefs.setIntPref(tier, priorTier);
  }
}
