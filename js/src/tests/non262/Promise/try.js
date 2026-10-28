// |reftest| async

async function testPromiseTry() {

const desc = Object.getOwnPropertyDescriptor(Promise, "try");
assertEq(typeof Promise.try, "function");
assertEq(desc.enumerable, false);
assertEq(desc.configurable, true);
assertEq(desc.writable, true);
assertEq(Promise.try.length, 1);
assertEq(Promise.try.name, "try");
assertEq(isConstructor(Promise.try), false);

let seen;
let result = Promise.try(function(a, b) {
  "use strict";
  seen = [this, a, b, arguments.length];
  return 42;
}, "first", "second");
assertEq(result instanceof Promise, true);
assertEq(seen[0], undefined);
assertEq(seen[1], "first");
assertEq(seen[2], "second");
assertEq(seen[3], 2);

let fulfilled;
result.then(value => fulfilled = value);
await result;
assertEq(fulfilled, 42);

const originalPromise = Promise.resolve("same");
assertEq(Promise.try(() => originalPromise), originalPromise);

const thenable = { then(resolve) { resolve("assimilated"); } };
let assimilated;
await Promise.try(() => thenable).then(value => assimilated = value);
assertEq(assimilated, "assimilated");

const thrown = { reason: true };
let rejected;
const rejectedPromise = Promise.try(() => { throw thrown; });
assertEq(rejectedPromise instanceof Promise, true);
await rejectedPromise.then(undefined, reason => rejected = reason);
assertEq(rejected, thrown);

let missingCallbackError;
await Promise.try().then(undefined, error => missingCallbackError = error);
assertEq(missingCallbackError instanceof TypeError, true);

let nonCallableError;
await Promise.try(1).then(undefined, error => nonCallableError = error);
assertEq(nonCallableError instanceof TypeError, true);

class SubPromise extends Promise {}
let subclassCalls = 0;
class ObservedPromise extends Promise {
  constructor(executor) {
    subclassCalls++;
    super(executor);
  }
}
const subclassResult = SubPromise.try(() => 7);
assertEq(subclassResult instanceof SubPromise, true);
const subclassOriginal = SubPromise.resolve(8);
assertEq(SubPromise.try(() => subclassOriginal), subclassOriginal);

let subclassRejected;
const rejectedSubclass = SubPromise.try(() => { throw thrown; });
assertEq(rejectedSubclass instanceof SubPromise, true);
await rejectedSubclass.then(undefined, reason => subclassRejected = reason);
assertEq(subclassRejected, thrown);

let observedRejected;
ObservedPromise.try(() => { throw thrown; }).then(undefined, reason => observedRejected = reason);
assertEq(subclassCalls, 2); // try constructs once; then constructs its result.
await Promise.resolve();
assertEq(observedRejected, thrown);

const throwingConstructor = function() { throw new Error("constructor second"); };
assertThrowsInstanceOf(() => Promise.try.call(null, () => 1), TypeError);
assertThrowsInstanceOf(() => Promise.try.call(throwingConstructor, () => 1), Error);
assertThrowsInstanceOf(() => Promise.try.call(throwingConstructor, () => {
  throw new Error("callback first");
}), Error);
let primitiveReceiverCallbackCalled = false;
assertThrowsInstanceOf(() => Promise.try.call(1, () => {
  primitiveReceiverCallbackCalled = true;
}), TypeError);
assertEq(primitiveReceiverCallbackCalled, false);
let nonConstructorCallbackCalled = false;
assertThrowsInstanceOf(() => Promise.try.call({}, () => {
  nonConstructorCallbackCalled = true;
}), TypeError);
assertEq(nonConstructorCallbackCalled, true);

// A generic capability can be an arbitrary object. It observes the callback
// before construction and receives the original reason after an intervening GC.
const order = [];
const marker = {};
let receiver, reason;
function Capability(executor) {
  order.push("construct");
  if (typeof gc === "function") gc();
  executor(function(value) {
    "use strict";
    receiver = this;
    reason = value;
    order.push("resolve");
  }, function(value) {
    "use strict";
    receiver = this;
    reason = value;
    order.push("reject");
  });
  return marker;
}
assertEq(Promise.try.call(Capability, () => {
  order.push("callback");
  throw thrown;
}), marker);
assertEq(order.join(","), "callback,construct,reject");
assertEq(receiver, undefined);
assertEq(reason, thrown);
order.length = 0;
assertEq(Promise.try.call(Capability, () => {
  order.push("callback");
  return thrown;
}), marker);
assertEq(order.join(","), "callback,construct,resolve");
assertEq(receiver, undefined);
assertEq(reason, thrown);

// Constructor failure supersedes callback failure, and reject-function failure
// is synchronous. Neither path may leave the callback exception pending.
const constructorError = {};
function Throwing() { throw constructorError; }
try {
  Promise.try.call(Throwing, () => { throw thrown; });
  assertEq(true, false);
} catch (error) {
  assertEq(error, constructorError);
}
const rejectError = {};
function RejectThrows(executor) {
  executor(() => {}, () => { throw rejectError; });
}
try {
  Promise.try.call(RejectThrows, () => { throw thrown; });
  assertEq(true, false);
} catch (error) {
  assertEq(error, rejectError);
}

// PromiseResolve first checks matching constructor identity, even when C
// itself is not constructible.
const nonConstructor = {};
const unwrapped = Promise.resolve(10);
unwrapped.constructor = nonConstructor;
assertEq(Promise.try.call(nonConstructor, () => unwrapped), unwrapped);
}

testPromiseTry().then(() => {
  if (typeof reportCompare === "function") reportCompare(0, 0);
  if (typeof $DONE === "function") $DONE();
}, error => {
  if (typeof $DONE === "function") $DONE(error);
  else throw error;
});
