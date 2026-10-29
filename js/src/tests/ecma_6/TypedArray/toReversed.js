for (var constructor of anyTypedArrayConstructors) {
    // Copying methods use the intrinsic constructor, not the shared-memory
    // subclass synthesized by shell.js, and produce an unshared buffer.
    var resultConstructor = this[constructor.name];
    assertEq(constructor.prototype.toReversed.length, 0);

    var original = new constructor([1, 2, 3, 4]);
    var reversed = original.toReversed();
    assertDeepEq(reversed, new resultConstructor([4, 3, 2, 1]));
    assertDeepEq(original, new constructor([1, 2, 3, 4]));
    assertEq(reversed === original, false);
    assertEq(reversed.constructor, resultConstructor);
    assertEq(reversed.buffer instanceof ArrayBuffer, true);

    assertDeepEq(new constructor().toReversed(), new resultConstructor());
    assertDeepEq(new constructor([1]).toReversed(), new resultConstructor([1]));
    assertDeepEq(new constructor([1, 2, 3]).toReversed(), new resultConstructor([3, 2, 1]));

    var other = new constructor([7, 8, 9]);
    Object.defineProperty(other, "length", {
        get() {
            throw new Error("length accessor called");
        }
    });
    assertDeepEq(other.toReversed(), new resultConstructor([9, 8, 7]));

    var ctorIgnored = new constructor([5, 6, 7]);
    Object.defineProperty(ctorIgnored, "constructor", {
        get() {
            throw new Error("constructor accessor called");
        }
    });
    assertDeepEq(ctorIgnored.toReversed(), new resultConstructor([7, 6, 5]));

    var invalidReceivers = [undefined, null, 1, false, "", Symbol(), [], {}, /./,
                            new Proxy(new constructor(), {})];
    invalidReceivers.forEach(invalidReceiver => {
        assertThrowsInstanceOf(() => {
            constructor.prototype.toReversed.call(invalidReceiver);
        }, TypeError,
        "Assert that toReversed fails if this value is not a TypedArray");
    });
}

for (var constructor of typedArrayConstructors) {
    if (typeof newGlobal === "function") {
        var otherGlobal = newGlobal();
        var otherConstructor = otherGlobal[constructor.name];
        var toReversed = otherConstructor.prototype.toReversed;
        var original = new constructor([3, 2, 1]);
        var reversed = toReversed.call(original);

        assertDeepEq(reversed, new otherConstructor([1, 2, 3]));
        assertEq(Object.getPrototypeOf(reversed), otherConstructor.prototype);
        assertDeepEq(original, new constructor([3, 2, 1]));
    }
}

if (typeof reportCompare === "function")
    reportCompare(true, true);
