// Warm each unary IC with int32 results before crossing its representation
// boundary. Both postfix preservation and the stored value must be correct.
function postInc(x) { let old = x++; return [old, x]; }
function preInc(x) { return ++x; }
function postDec(x) { let old = x--; return [old, x]; }
function preDec(x) { return --x; }
for (let i = 0; i < 100; i++) {
    assertEq(postInc(10)[1], 11);
    assertEq(preInc(10), 11);
    assertEq(postDec(10)[1], 9);
    assertEq(preDec(10), 9);
}
for (let i = 0; i < 20; i++) {
    let a = postInc(2147483647);
    assertEq(a[0], 2147483647);
    assertEq(a[1], 2147483648);
    assertEq(preInc(2147483647), 2147483648);
    let b = postDec(-2147483648);
    assertEq(b[0], -2147483648);
    assertEq(b[1], -2147483649);
    assertEq(preDec(-2147483648), -2147483649);
}
