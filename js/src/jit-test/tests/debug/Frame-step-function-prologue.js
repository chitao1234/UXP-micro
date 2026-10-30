// Function setup is not a breakpoint, but observable parameter initializers
// must remain in the executable section. Offset reflection must agree with
// the actual breakpoint list even while onStep visits prologue bytecode.
var g = newGlobal();
var dbg = new Debugger(g);

g.eval("var parameterEffects = 0;\n" +
       "function parameterEffect() { parameterEffects++; return 17; }\n" +
       "function withThis(value) {\n" +
       "  this.savedThisValue = value;\n" +
       "  return value;\n" +
       "}\n" +
       "function withArguments(value) {\n" +
       "  return arguments[0];\n" +
       "}\n" +
       "function withClosure(value) {\n" +
       "  return () => value;\n" +
       "}\n" +
       "function withDefault(\n" +
       "  value = parameterEffect()\n" +
       ") {\n" +
       "  this.savedDefaultValue = value;\n" +
       "  return value;\n" +
       "}\n" +
       "function withDestructuring({value}) {\n" +
       "  return value;\n" +
       "}\n");

var expectedBodyLines = { withThis: 4, withArguments: 8, withClosure: 11 };
var visited = new Set();
var prologueScripts = 0;
var firstDefaultEntry = false;
dbg.onEnterFrame = frame => {
    if (!frame.callee)
        return;
    const name = frame.callee.name;
    if (!name || name.indexOf("with") != 0)
        return;
    const offsets = new Map(frame.script.getAllColumnOffsets()
                                 .map(entry => [entry.offset, entry]));
    if (Math.min(...offsets.keys()) > 0) {
        prologueScripts++;
        assertEq(frame.script.getOffsetLocation(0).isEntryPoint, false);
    }
    let first = true;
    frame.onStep = function() {
        const location = this.script.getOffsetLocation(this.offset);
        assertEq(location.isEntryPoint, offsets.has(this.offset));
        if (!location.isEntryPoint)
            return;
        const entry = offsets.get(this.offset);
        assertEq(location.lineNumber, entry.lineNumber);
        assertEq(location.columnNumber, entry.columnNumber);
        if (first) {
            first = false;
            visited.add(name);
            if (name in expectedBodyLines)
                assertEq(location.lineNumber, expectedBodyLines[name]);
            if (name == "withDefault") {
                firstDefaultEntry = true;
                assertEq(g.parameterEffects, 0);
                assertEq(g.savedDefaultValue, undefined);
            }
        }
    };
};

assertEq(g.withThis(3), 3);
assertEq(g.savedThisValue, 3);
assertEq(g.withArguments(5), 5);
assertEq(g.withClosure(7)(), 7);
assertEq(g.withDefault(), 17);
assertEq(g.savedDefaultValue, 17);
assertEq(g.parameterEffects, 1);
assertEq(g.withDestructuring({ value: 19 }), 19);
assertEq(prologueScripts > 0, true);
assertEq(firstDefaultEntry, true);
assertEq(visited.size, 5);
