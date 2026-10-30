let Array_unscopables = Array.prototype[Symbol.unscopables];

let desc = Reflect.getOwnPropertyDescriptor(Array.prototype, Symbol.unscopables);
assertDeepEq(desc, {
    value: Array_unscopables,
    writable: false,
    enumerable: false,
    configurable: true
});

assertEq(Reflect.getPrototypeOf(Array_unscopables), null);

let desc2 = Object.getOwnPropertyDescriptor(Array_unscopables, "values");
assertDeepEq(desc2, {
    value: true,
    writable: true,
    enumerable: true,
    configurable: true
});

let keys = Reflect.ownKeys(Array_unscopables);
print(uneval(keys));
// The property set is specified; its creation order is not.
assertDeepEq(keys.slice().sort(), [
    "copyWithin", "entries", "fill", "find", "findIndex", "findLast",
    "findLastIndex", "flat", "flatMap", "includes", "keys", "toReversed",
    "toSorted", "toSpliced", "values", "with"
].sort());

for (let key of keys)
    assertEq(Array_unscopables[key], true);

// Test that it actually works
assertThrowsInstanceOf(() => {
    with ([]) {
        return entries;
    }
}, ReferenceError);

{
    let fill = 33;
    with (Array.prototype) {
        assertEq(fill, 33);
    }
}

reportCompare(0, 0);
