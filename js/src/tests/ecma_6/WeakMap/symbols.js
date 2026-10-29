/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/licenses/publicdomain/ */

// Non-registered and well-known symbols can be held weakly.
var m = new WeakMap;
var sym = Symbol();
assertEq(m.set(sym, 0), m);
assertEq(m.has(sym), true);
assertEq(m.get(sym), 0);
assertEq(m.has(Symbol()), false);

m.set(Symbol.hasInstance, 2);
assertEq(m.get(Symbol.hasInstance), 2);
assertEq(new WeakMap([[sym, 3]]).get(sym), 3);

// Registered symbols have process-wide identities and cannot be weak keys.
var registered = Symbol.for("weak-map-test");
assertThrowsInstanceOf(() => m.set(registered, 1), TypeError);
assertThrowsInstanceOf(() => new WeakMap([[registered, 1]]), TypeError);
assertEq(m.has(registered), false);
assertEq(m.get(registered), undefined);
assertEq(m.delete(registered), false);

assertEq(m.delete(sym), true);
assertEq(m.has(sym), false);
assertEq(m.get(sym), undefined);

if (typeof reportCompare === "function")
  reportCompare(0, 0);
