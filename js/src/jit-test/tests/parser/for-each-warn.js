// UXP intentionally retains for-each without deprecation warning spew (d9604196864).

function testWarn(code) {
  enableLastWarning();
  var g = newGlobal();
  g.code = code;
  g.eval('eval(code)');
  var warning = getLastWarning();
  assertEq(warning, null, "for-each should not produce a deprecation warning for " + code);

  clearLastWarning();
  g.eval('eval(code)');
  warning = getLastWarning();
  assertEq(warning, null, "warning should not be caught for 2nd ocurrence");

  clearLastWarning();
  g = newGlobal();
  g.code = code;
  g.eval('Reflect.parse(code);');
  warning = getLastWarning();
  assertEq(warning, null, "for-each should not produce a deprecation warning for " + code);

  clearLastWarning();
  g.eval('Reflect.parse(code);');
  warning = getLastWarning();
  assertEq(warning, null, "warning should not be caught for 2nd ocurrence");
  disableLastWarning();
}

testWarn("for each (var x in {}) {}");
testWarn("for each (x in {}) {}");
testWarn("for each (let y in {}) {}");
testWarn("for each (const y in {}) {}");
testWarn("for each ([x, y] in {}) {}");
