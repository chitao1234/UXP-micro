// |reftest| skip-if(!xulRuntime.shell)
// Every assignment operator must retain its own spelling in the AST.
for (let op of ["=", "+=", "-=", "*=", "/=", "%=", "**=", "<<=", ">>=",
                ">>>=", "|=", "^=", "&=", "??=", "||=", "&&="]) {
    let node = Reflect.parse("a " + op + " b").body[0].expression;
    assertEq(node.type, "AssignmentExpression");
    assertEq(node.operator, op);
    assertEq(node.left.name, "a");
    assertEq(node.right.name, "b");
}
reportCompare(0, 0);
