// Native Function.prototype must not expose an interpreted frame/environment.
// Keep real function entry coverage so the absence of the native hook is not
// a vacuous result from an inactive debugger.

var g = newGlobal();
var dbg = new Debugger(g);
var entries = 0;
dbg.onEnterFrame = function (frame) {
    assertEq(frame.environment instanceof Debugger.Environment, true);
    entries++;
};
assertEq(g.Function.prototype(), undefined);
assertEq(entries, 0);
g.eval('(function userFunction() {})()');
assertEq(entries, 2); // The eval script and the user function.
