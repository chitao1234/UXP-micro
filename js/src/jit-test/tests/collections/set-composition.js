// All seven Set methods use internal receiver data and the other operand's
// set-like protocol. These regressions supplement Test262's method cases.
var methods = ["union", "intersection", "difference", "symmetricDifference",
               "isSubsetOf", "isSupersetOf", "isDisjointFrom"];
function values(set) { return Array.from(set); }
function same(actual, expected) {
    assertEq(actual.length, expected.length);
    for (var i = 0; i < actual.length; i++)
        assertEq(actual[i], expected[i]);
}
function throws(ctor, f) {
    var threw = false;
    try { f(); } catch (e) { assertEq(e instanceof ctor, true); threw = true; }
    assertEq(threw, true);
}
function like(size, keys, has) { return {size, keys, has: has || function() { return false; }}; }

for (var method of methods) {
    // Check brand before any observable property access on other.
    var touched = false;
    var other = {get size() { touched = true; throw 1; }};
    throws(TypeError, () => Set.prototype[method].call({}, other));
    throws(TypeError, () => Set.prototype[method].call(new Proxy(new Set(), {}), other));
    assertEq(touched, false);

    for (var size of [-1, -Infinity])
        throws(RangeError, () => new Set()[method](like(size, function*() {})));
    for (var size of [NaN, undefined, 1n, Symbol()])
        throws(TypeError, () => new Set()[method](like(size, function*() {})));
    // ToInteger truncates -0.5 toward zero before checking negativity.
    new Set()[method](like(-0.5, function*() {}));
}

// Getter ordering: keys and next are captured before union copies its receiver.
{
    var receiver = new Set([1]);
    var order = [];
    var nextCalls = 0;
    var other = {
        get size() { order.push("size"); return {valueOf() { order.push("number"); return 1; }}; },
        get has() { order.push("has"); return function() { throw "not called"; }; },
        get keys() {
            order.push("keys"); receiver.add(2);
            return function() {
                order.push("keys()"); receiver.add(3);
                return {get next() {
                    order.push("next"); receiver.add(4);
                    return function() {
                        nextCalls++;
                        return nextCalls === 1 ? {done: false, value: 5} : {done: true};
                    };
                }};
            };
        }
    };
    same(values(receiver.union(other)), [1, 2, 3, 4, 5]);
    same(order, ["size", "number", "has", "keys", "keys()", "next"]);
}

// Short circuit closes other.keys once. A normal completion checks that return
// returns an object; abrupt next/done/value completion does not call return.
for (var method of ["isSupersetOf", "isDisjointFrom"]) {
    var closes = 0;
    var badValue = method === "isSupersetOf" ? 9 : 1;
    var other = like(0, function() {
        return {next() { return {done: false, value: badValue}; },
                return() { closes++; return {}; }};
    });
    assertEq(new Set([1])[method](other), false);
    assertEq(closes, 1);
    other.keys = function() { return {next() { return {done: false, value: badValue}; },
                                    return() { return 7; }}; };
    throws(TypeError, () => new Set([1])[method](other));
    for (let part of ["next", "done", "value"]) {
        closes = 0;
        other.keys = function() {
            return {next() {
                if (part === "next") throw new RangeError("next");
                return {get done() { if (part === "done") throw new RangeError("done"); return false; },
                        get value() { throw new RangeError("value"); }};
            }, return() { closes++; return {}; }};
        };
        throws(RangeError, () => new Set([1])[method](other));
        assertEq(closes, 0);
    }
}

// A receiver scan observes deletion/addition during the other.has callback.
{
    var receiver = new Set([1, 2]);
    var other = like(3, function() { throw "not called"; }, function(v) {
        if (v === 1) { receiver.delete(2); receiver.add(3); }
        return true;
    });
    same(values(receiver.intersection(other)), [1, 3]);
}
// difference scans the copied result, so the same source mutation cannot alter
// which original entries it tests.
{
    var receiver = new Set([1, 2]);
    var tested = [];
    var other = like(3, function() { throw "not called"; }, function(v) {
        tested.push(v); receiver.delete(2); receiver.add(3); return false;
    });
    same(values(receiver.difference(other)), [1, 2]);
    same(tested, [1, 2]);
}

// Duplicate set-like keys must not toggle symmetric difference repeatedly;
// SameValueZero canonically treats zero and NaN.
{
    var other = like(5, function*() { yield 1; yield 1; yield 2; yield 2; yield -0; yield NaN; });
    var result = new Set([1, NaN]).symmetricDifference(other);
    same(values(result), [2, 0]);
    assertEq(1 / values(result)[1], Infinity);
}

// Results ignore constructor/species, receiver adder and iterator overrides.
{
    class Derived extends Set {
        static get [Symbol.species]() { throw "species"; }
    }
    var receiver = new Derived([1, 2]);
    Object.defineProperty(receiver, "constructor", {get() { throw "constructor"; }});
    receiver[Symbol.iterator] = receiver.values = receiver.add = function() { throw "override"; };
    for (var method of ["union", "intersection", "difference", "symmetricDifference"]) {
        var result = receiver[method](new Set([2, 3]));
        assertEq(Object.getPrototypeOf(result), Set.prototype);
    }
}

// A cached next method is not re-read or replaced by a later iteration.
{
    var calls = 0;
    var iterator = {next() { this.next = function() { throw "changed next"; };
                            return calls++ ? {done: true} : {done: false, value: 4}; }};
    same(values(new Set().union(like(0, () => iterator))), [4]);
}

// Private iterator state must not trigger a content Object.prototype setter.
{
    var calls = 0;
    Object.defineProperty(Object.prototype, "value", {configurable: true,
        set(v) { calls++; throw "prototype setter"; }});
    try {
        same(values(new Set([1]).union(like(1, function*() { yield 2; }))), [1, 2]);
        assertEq(calls, 0);
    } finally {
        delete Object.prototype.value;
    }
}

if (typeof gc === "function") {
    // Moving collections while a user-supplied has callback is active must
    // preserve object keys and the live receiver iteration.
    var key = {};
    var receiver = new Set([key]);
    var result = receiver.intersection(like(1, function*() {}, function(v) {
        gc(); return v === key;
    }));
    assertEq(result.has(key), true);
}
