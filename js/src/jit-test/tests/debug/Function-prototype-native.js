// An otherwise empty debuggee has no synthetic Function.prototype script.
// Native calls do not become script frames, but author code with identical
// empty-function text remains fully visible to the debugger.
var g = newGlobal();
var dbg = new Debugger();
var gw = dbg.addDebuggee(g);
var fp = gw.getOwnPropertyDescriptor("Function").value.proto;
assertEq(fp.callable, true);
assertEq(fp.name, "");
assertEq(fp.script, undefined);
assertEq(fp.environment, undefined);
assertEq(dbg.findScripts({ global: g }).length, 0);

var entries = [];
var newScripts = [];
dbg.onEnterFrame = frame => { entries.push(frame.script); };
dbg.onNewScript = script => { newScripts.push(script); };
assertEq(fp.call(null).return, undefined);
assertEq(entries.length, 0);
assertEq(newScripts.length, 0);

var user = gw.executeInGlobal("(function () {\n})").return;
var script = user.script;
assertEq(script instanceof Debugger.Script, true);
assertEq(script.source.text, "(function () {\n})");
assertEq(dbg.findScripts({ source: script.source }).indexOf(script) >= 0, true);
assertEq(newScripts.length > 0, true);
entries.length = 0;
assertEq(user.call(null).return, undefined);
assertEq(entries.length, 1);
assertEq(entries[0], script);

// Holding debugger wrappers through GC must preserve the real source while
// leaving the native prototype without a fabricated script or environment.
gc();
assertEq(user.script, script);
assertEq(script.source.text, "(function () {\n})");
assertEq(fp.script, undefined);
assertEq(fp.environment, undefined);
