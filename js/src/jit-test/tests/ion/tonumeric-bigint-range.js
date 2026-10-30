// |jit-test| --ion-check-range-analysis

function negate(value) { return -value; }
function increment(value) { return ++value; }
function decrement(value) { return --value; }
function bitwise(a, b) { return [a & b, a | b, a ^ b, ~a, a << b, a >> b]; }

var large = 1n << 100n;
var boxed = { valueOf() { return large; } };
for (var i = 0; i < 100; i++) {
    assertEq(negate(large), -large);
    assertEq(increment(large), large + 1n);
    assertEq(decrement(large), large - 1n);
    assertEq(negate(boxed), -large);
    assertEq(increment(boxed), large + 1n);
    assertEq(decrement(boxed), large - 1n);
    var result = bitwise(large, 2n);
    assertEq(result[0], 0n);
    assertEq(result[1], large + 2n);
    assertEq(result[2], large + 2n);
    assertEq(result[3], -large - 1n);
    assertEq(result[4], large * 4n);
    assertEq(result[5], large / 4n);
}

// Type changes must still produce Number results and preserve negative zero.
for (var i = 0; i < 100; i++) {
    assertEq(negate(0), -0);
    assertEq(increment(42), 43);
    assertEq(decrement(42), 41);
}
