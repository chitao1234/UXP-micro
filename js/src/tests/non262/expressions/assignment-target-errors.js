// Invalid assignment targets must be rejected before evaluating any code.

const invalidTargets = [
    "obj?.x = 1",
    "obj?.[key] = 1",
    "obj?.() = 1",
    "(obj?.x) = 1",
    "obj?.x.y = 1",
    "obj?.x += 1",
    "obj?.x &&= 1",
    "[obj?.x = 1] = []",
    "({x: obj?.x = 1} = {})",
    "for ([obj?.x = 1] in []) {}",
    "for ({x: obj?.x = 1} of []) {}",
    "f() &&= 1",
    "f() ||= 1",
    "f() ??= 1",
];

let evaluated = false;
for (let target of invalidTargets) {
    // Test the full parser, Function constructor, and lazy inner-function parse.
    assertThrowsInstanceOf(() => eval("evaluated = true; " + target), SyntaxError);
    assertEq(evaluated, false);
    assertThrowsInstanceOf(() => Function(target), SyntaxError);
    assertThrowsInstanceOf(() => Function("function inner() { " + target + "; }"),
                           SyntaxError);
    assertThrowsInstanceOf(() => Function("'use strict'; " + target), SyntaxError);
}

// A property of the completed chain result is still a valid target.
let obj = {x: {y: 0}};
(obj?.x).y = 42;
assertEq(obj.x.y, 42);

// Preserve the existing sloppy-mode compatibility path for call assignments:
// the call is evaluated, then the runtime throws ReferenceError.
let calls = 0;
function f() { calls++; return 1; }
assertThrowsInstanceOf(() => eval("f() = 2"), ReferenceError);
assertEq(calls, 1);
assertThrowsInstanceOf(() => eval("f() += 2"), ReferenceError);
assertEq(calls, 2);
assertThrowsInstanceOf(() => Function("'use strict'; f() = 2"), SyntaxError);
assertThrowsInstanceOf(() => eval("1 = 2"), ReferenceError);

reportCompare(0, 0);
