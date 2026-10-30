// Native Function.prototype has no script source. Real user functions still
// have fully initialized introduction metadata, even when their source looks
// like the former internal prototype script.
var g = newGlobal();
var dbg = new Debugger;
var gw = dbg.addDebuggee(g);

var DOfp = gw.getOwnPropertyDescriptor('Function').value.proto;
assertEq(DOfp.script, undefined);
assertEq(DOfp.environment, undefined);
assertEq(dbg.findScripts().length, 0);

var userFunction = gw.executeInGlobal('(function () {\n})').return;
assertEq(userFunction.script instanceof Debugger.Script, true);
assertEq(userFunction.script.source.text, '(function () {\n})');
assertEq(userFunction.script.source.introductionScript, undefined);
assertEq(userFunction.script.source.introductionOffset, undefined);
assertEq(dbg.findScripts().indexOf(userFunction.script) >= 0, true);
