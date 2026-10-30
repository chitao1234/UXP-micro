// |jit-test| skip-if: !this.TypedObject
// A TypedObject field has no native Shape. Reusing a getprop IC that has
// already seen a native accessor must not reinterpret the field as a Shape.
const Pair = new TypedObject.StructType({x: TypedObject.int32});
function readX(obj) { return obj.x; }
for (let i = 0; i < 200; i++) {
    assertEq(readX({get x() { return 7; }}), 7);
    assertEq(readX(new Pair({x: 42})), 42);
    assertEq(readX(new Pair({x: -3})), -3);
}
