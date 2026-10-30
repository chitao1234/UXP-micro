// Hashbang probing must preserve leading line terminators and never run in
// a nested statement list. Check syntax, runtime and Reflect.parse positions.
for (let newline of ["\n", "\r", "\r\n", "\u2028", "\u2029"]) {
    assertEq(evaluate(newline + "saveStack().line"), 2);
    assertEq(evaluate(newline + "saveStack().column", {columnNumber: 1729}), 1);
    let stmt = Reflect.parse(newline + "x;").body[0];
    assertEq(stmt.loc.start.line, 2);
    assertEq(stmt.loc.start.column, 0);
    try {
        evaluate(newline + "let a, a;");
        assertEq(true, false);
    } catch (e) {
        assertEq(e instanceof SyntaxError, true);
        assertEq(e.lineNumber, 2);
    }
    assertEq(evaluate("#!/usr/bin/js" + newline + "saveStack().line"), 2);
}
assertEq(evaluate(""), undefined);
assertEq(evaluate("#!"), undefined);
assertEq(evaluate("{ let x = 1; x; }"), 1);
for (let source of ["#", "{#!invalid\n}", "\n#!invalid", "x;#!invalid"]) {
    let threw = false;
    try { evaluate(source); } catch (e) { threw = e instanceof SyntaxError; }
    assertEq(threw, true);
}
