// Test that a frame's onStep handler gets called at least once on each line of a function.

var g = newGlobal();
var dbg = new Debugger(g);

// When we hit a 'debugger' statement, set offsets to the frame's script's
// table of line offsets --- a sparse array indexed by line number. Begin
// single-stepping the current frame; for each source line we hit, delete
// the line's entry in offsets. Thus, at the end, offsets is an array with
// an element for each line we did not reach.
var doSingleStep = true;
var offsets;
dbg.onDebuggerStatement = function (frame) {
    var script = frame.script;
    offsets = script.getAllOffsets();
    if (frame.callee && frame.callee.name == "t") {
        // Function setup belongs in the prologue; the declaration header is
        // not an executable line of this function.
        assertEq(Object.prototype.hasOwnProperty.call(offsets, 1), false);
    }
    print("debugger line: " + script.getOffsetLocation(frame.offset).lineNumber);
    print("original lines: " + uneval(Object.keys(offsets)));
    if (doSingleStep) {
	frame.onStep = function onStepHandler() {
	    var line = script.getOffsetLocation(this.offset).lineNumber;
	    delete offsets[line];
	};
    }
};

g.eval(
       'function t(a, b, c) {                \n' +
       '    debugger;                        \n' +
       '    var x = a;                       \n' +
       '    x += b;                          \n' +
       '    if (x < 10)                      \n' +
       '        x -= c;                      \n' +
       '    return x;                        \n' +
       '}                                    \n'
       );

// The handler starts after the debugger opcode on line 2. That line remains
// unvisited; the function header was never an executable offset.
g.eval('t(1,2,3)');
assertEq(JSON.stringify(Object.keys(offsets)), '["2"]');

// This also leaves the unexecuted body of the 'if' on line 6 unvisited.
g.eval('t(10,20,30)');
assertEq(JSON.stringify(Object.keys(offsets)), '["2","6"]');

// This shouldn't stop at all. It's the frame that's in single-step mode,
// not the script, so the prior execution of t in single-step mode should
// have no effect on this one.
doSingleStep = false;
g.eval('t(0, 0, 0)');
assertEq(JSON.stringify(Object.keys(offsets)), '["2","3","4","5","6","7","8"]');
doSingleStep = true;

// Single-step in an eval frame. This should reach every line but the
// first.
g.eval(
       'debugger;                        \n' +
       'var a=1, b=2, c=3;               \n' +
       'var x = a;                       \n' +
       'x += b;                          \n' +
       'if (x < 10)                      \n' +
       '    x -= c;                      \n'
       );
print("final lines: " + uneval(Object.keys(offsets)));
assertEq(Object.keys(offsets).length, 1);

// Single-step in a global code frame. This should reach every line but the
// first.
g.evaluate(
           'debugger;                        \n' +
           'var a=1, b=2, c=3;               \n' +
           'var x = a;                       \n' +
           'x += b;                          \n' +
           'if (x < 10)                      \n' +
           '    x -= c;                      \n'
           );
print("final lines: " + uneval(Object.keys(offsets)));
assertEq(Object.keys(offsets).length, 1);
