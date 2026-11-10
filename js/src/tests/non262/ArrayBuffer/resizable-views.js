// |reftest| skip-if(!this.SharedArrayBuffer)

var rab = new ArrayBuffer(4, { maxByteLength: 16 });
var tracking = new Uint8Array(rab);
var fixed = new Uint8Array(rab, 1, 2);
var bytes = new Uint8Array(rab);

bytes[1] = 11;
bytes[2] = 22;

assertEq(tracking.length, 4);
assertEq(tracking.byteLength, 4);
assertEq(tracking.byteOffset, 0);
assertEq(fixed.length, 2);
assertEq(fixed.byteLength, 2);
assertEq(fixed.byteOffset, 1);

rab.resize(2);
assertEq(tracking.length, 2);
assertEq(tracking.byteLength, 2);
assertEq(fixed.length, 0);
assertEq(fixed.byteLength, 0);
assertEq(fixed.byteOffset, 0);
assertEq(fixed[0], undefined);

rab.resize(8);
assertEq(tracking.length, 8);
assertEq(tracking.byteLength, 8);
assertEq(fixed.length, 2);
assertEq(fixed.byteLength, 2);
assertEq(fixed.byteOffset, 1);
assertEq(fixed[0], 11);
assertEq(fixed[1], 0);
tracking[6] = 66;
assertEq(new Uint8Array(rab)[6], 66);

var dv = new DataView(rab, 4);
assertEq(dv.byteOffset, 4);
assertEq(dv.byteLength, 4);
dv.setUint8(0, 44);
assertEq(tracking[4], 44);

rab.resize(3);
assertThrowsInstanceOf(() => dv.byteOffset, TypeError);
assertThrowsInstanceOf(() => dv.byteLength, TypeError);
assertThrowsInstanceOf(() => dv.getUint8(0), TypeError);

rab.resize(6);
assertEq(dv.byteOffset, 4);
assertEq(dv.byteLength, 2);
assertEq(dv.getUint8(0), 0);

var fixedDv = new DataView(rab, 4, 2);
rab.resize(5);
assertThrowsInstanceOf(() => fixedDv.byteOffset, TypeError);
assertThrowsInstanceOf(() => fixedDv.byteLength, TypeError);
assertThrowsInstanceOf(() => fixedDv.getUint8(0), TypeError);
rab.resize(6);
assertEq(fixedDv.byteOffset, 4);
assertEq(fixedDv.byteLength, 2);

var methodRab = new ArrayBuffer(4, { maxByteLength: 8 });
var methodFixed = new Uint8Array(methodRab, 2, 2);
methodRab.resize(2);
[
  () => methodFixed.at(0),
  () => methodFixed.copyWithin(0, 0),
  () => methodFixed.entries(),
  () => methodFixed.every(x => true),
  () => methodFixed.fill(1),
  () => methodFixed.filter(x => true),
  () => methodFixed.find(x => true),
  () => methodFixed.findIndex(x => true),
  () => methodFixed.findLast(x => true),
  () => methodFixed.findLastIndex(x => true),
  () => methodFixed.forEach(x => x),
  () => methodFixed.includes(0),
  () => methodFixed.indexOf(0),
  () => methodFixed.join(","),
  () => methodFixed.keys(),
  () => methodFixed.lastIndexOf(0),
  () => methodFixed.map(x => x),
  () => methodFixed.reduce((a, b) => a + b, 0),
  () => methodFixed.reduceRight((a, b) => a + b, 0),
  () => methodFixed.reverse(),
  () => methodFixed.set([1], 0),
  () => methodFixed.slice(),
  () => methodFixed.some(x => true),
  () => methodFixed.sort(),
  () => methodFixed.subarray(),
  () => methodFixed.toLocaleString(),
  () => methodFixed.toReversed(),
  () => methodFixed.toSorted(),
  () => methodFixed.toString(),
  () => methodFixed.values(),
  () => methodFixed.with(0, 1),
  () => methodFixed[Symbol.iterator](),
].forEach(fn => assertThrowsInstanceOf(fn, TypeError));

methodRab.resize(4);
assertEq(methodFixed.length, 2);

var sourceRab = new ArrayBuffer(4, { maxByteLength: 8 });
var oobSource = new Uint8Array(sourceRab, 2, 2);
sourceRab.resize(2);
assertThrowsInstanceOf(() => new Uint8Array(oobSource), TypeError);
assertThrowsInstanceOf(() => new Uint16Array(oobSource), TypeError);
assertThrowsInstanceOf(() => new Uint8Array(4).set(oobSource), TypeError);
sourceRab.resize(4);
assertEq(new Uint8Array(oobSource).length, 2);

var ctorRab = new ArrayBuffer(8, { maxByteLength: 8 });
var ShrinkingNewTarget = new Proxy(function() {}, {
  get(target, prop, receiver) {
    if (prop === "prototype") {
      ctorRab.resize(2);
      return DataView.prototype;
    }
    return Reflect.get(target, prop, receiver);
  }
});
assertThrowsInstanceOf(() => Reflect.construct(DataView, [ctorRab, 4], ShrinkingNewTarget),
                       RangeError);

var fixedCtorRab = new ArrayBuffer(8, { maxByteLength: 8 });
var FixedShrinkingNewTarget = new Proxy(function() {}, {
  get(target, prop, receiver) {
    if (prop === "prototype") {
      fixedCtorRab.resize(5);
      return DataView.prototype;
    }
    return Reflect.get(target, prop, receiver);
  }
});
assertThrowsInstanceOf(() => Reflect.construct(DataView, [fixedCtorRab, 4, 2],
                                               FixedShrinkingNewTarget),
                       RangeError);

var gsab = new SharedArrayBuffer(4, { maxByteLength: 16 });
var sharedTracking = new Uint8Array(gsab);
assertEq(sharedTracking.length, 4);
gsab.grow(8);
assertEq(sharedTracking.length, 8);
sharedTracking[6] = 33;
assertEq(new Uint8Array(gsab)[6], 33);

var sharedDv = new DataView(gsab);
assertEq(sharedDv.buffer, gsab);
assertEq(sharedDv.byteOffset, 0);
assertEq(sharedDv.byteLength, 8);
sharedDv.setUint8(7, 99);
assertEq(sharedTracking[7], 99);
gsab.grow(12);
assertEq(sharedDv.byteLength, 12);
assertEq(sharedDv.getUint8(7), 99);

var fixedSharedDv = new DataView(gsab, 4, 2);
assertEq(fixedSharedDv.byteOffset, 4);
assertEq(fixedSharedDv.byteLength, 2);
gsab.grow(16);
assertEq(fixedSharedDv.byteOffset, 4);
assertEq(fixedSharedDv.byteLength, 2);

function readLength(view) {
  return view.length;
}

function readElement(view, index) {
  return view[index];
}

function writeElement(view, index, value) {
  view[index] = value;
}

var normal = new Uint8Array(4);
normal[0] = 7;
for (var i = 0; i < 2000; i++) {
  assertEq(readLength(normal), 4);
  assertEq(readElement(normal, 0), 7);
  writeElement(normal, 1, 8);
}

var icRab = new ArrayBuffer(4, { maxByteLength: 8 });
var icTracking = new Uint8Array(icRab);
icTracking[0] = 9;
assertEq(readLength(icTracking), 4);
assertEq(readElement(icTracking, 0), 9);
icRab.resize(0);
assertEq(readLength(icTracking), 0);
assertEq(readElement(icTracking, 0), undefined);
writeElement(icTracking, 0, 1);
icRab.resize(4);
assertEq(readLength(icTracking), 4);
assertEq(readElement(icTracking, 0), 0);
writeElement(icTracking, 0, 12);
assertEq(readElement(icTracking, 0), 12);

var icFixed = new Uint8Array(icRab, 1, 2);
assertEq(readLength(icFixed), 2);
icRab.resize(2);
assertEq(readLength(icFixed), 0);
assertEq(readElement(icFixed, 0), undefined);
writeElement(icFixed, 0, 55);
icRab.resize(4);
assertEq(readLength(icFixed), 2);
assertEq(readElement(icFixed, 0), 0);

var icGsab = new SharedArrayBuffer(4, { maxByteLength: 8 });
var icSharedTracking = new Uint8Array(icGsab);
assertEq(readLength(icSharedTracking), 4);
icGsab.grow(8);
assertEq(readLength(icSharedTracking), 8);
writeElement(icSharedTracking, 5, 77);
assertEq(readElement(icSharedTracking, 5), 77);

// Adapted from current Firefox's typedarray/resizable-typedarray-{get,set}-elem
// tests. Warm each access on fixed buffers first: the same scalar class can
// also contain RAB/GSAB views, so a class/type guard alone is insufficient.
function checkElementSpecialization(TA) {
  var read = Function("view", "index", "return view[index];");
  var write = Function("view", "index", "value", "view[index] = value;");
  var readInBounds = Function("view", "index", "return view[index];");
  var writeInBounds = Function("view", "index", "value", "view[index] = value;");
  var normal = new TA(4);
  var expected = new TA(4);
  var scalar = typeof normal[0] === "bigint" ? BigInt : Number;

  function check(view) {
    for (var i = 0; i < 200; i++) {
      var index = i % 8;
      var value = scalar(i);
      write(view, index, value);
      expected[index] = value;
      assertEq(read(view, index), expected[index]);
    }
  }

  // Exercise both the in-bounds and the hole paths before changing buffer kind.
  for (var i = 0; i < 4000; i++) {
    var index = i % 8;
    write(normal, index, scalar(i));
    expected[index] = scalar(i);
    assertEq(read(normal, index), expected[index]);
    var inBounds = i % 4;
    writeInBounds(normal, inBounds, scalar(i));
    expected[inBounds] = scalar(i);
    assertEq(readInBounds(normal, inBounds), expected[inBounds]);
  }

  var size = TA.BYTES_PER_ELEMENT;
  var buffer = new ArrayBuffer(4 * size, { maxByteLength: 8 * size });
  var tracking = new TA(buffer);
  var fixed = new TA(buffer, size, 2);
  writeInBounds(tracking, 0, scalar(13));
  assertEq(readInBounds(tracking, 0), scalar(13));
  writeInBounds(fixed, 0, scalar(14));
  assertEq(readInBounds(fixed, 0), scalar(14));
  check(tracking);
  buffer.resize(0);
  assertEq(read(tracking, 0), undefined);
  assertEq(read(fixed, 0), undefined);
  write(tracking, 0, scalar(19));
  write(fixed, 0, scalar(20));
  buffer.resize(8 * size);
  assertEq(read(tracking, 0), scalar(0));
  assertEq(read(fixed, 0), scalar(0));
  write(fixed, 1, scalar(31));
  assertEq(read(tracking, 2), scalar(31));
  write(tracking, 7, scalar(41));
  assertEq(read(tracking, 7), scalar(41));
  assertEq(read(fixed, 2), undefined);

  var shared = new SharedArrayBuffer(4 * size, { maxByteLength: 8 * size });
  var sharedTracking = new TA(shared);
  var sharedFixed = new TA(shared, size, 2);
  check(sharedTracking);
  shared.grow(8 * size);
  write(sharedTracking, 7, scalar(51));
  assertEq(read(sharedTracking, 7), scalar(51));
  write(sharedFixed, 1, scalar(61));
  assertEq(read(sharedTracking, 2), scalar(61));
  assertEq(read(sharedFixed, 2), undefined);
}

var elementTypes = [Int8Array, Uint8Array, Uint8ClampedArray, Int16Array,
                    Uint16Array, Int32Array, Uint32Array, Float32Array, Float64Array];
if (typeof Float16Array === "function")
  elementTypes.push(Float16Array);
if (typeof BigInt64Array === "function")
  elementTypes.push(BigInt64Array, BigUint64Array);
for (var TA of elementTypes)
  checkElementSpecialization(TA);

// Keep a separate in-bounds-only call site, including across effectful calls.
var readAfterCall = Function("view", "callback", "callback(); return view[0];");
var writeAfterCall = Function("view", "callback", "callback(); view[0] = 27;");
function noEffect() {}
for (var i = 0; i < 4000; i++) {
  assertEq(readAfterCall(normal, noEffect), 7);
  writeAfterCall(normal, noEffect);
  normal[0] = 7;
}
var effectBuffer = new ArrayBuffer(4, { maxByteLength: 8 });
var effectView = new Uint8Array(effectBuffer);
assertEq(readAfterCall(effectView, () => effectBuffer.resize(0)), undefined);
writeAfterCall(effectView, () => effectBuffer.resize(4));
assertEq(effectView[0], 27);

// Conversion must precede the store even when it detaches/resizes the buffer.
var convertingStore = Function("view", "value", "view[0] = value;");
for (var i = 0; i < 4000; i++)
  convertingStore(normal, i);
convertingStore(effectView, { valueOf() { effectBuffer.resize(0); return 73; } });
assertEq(effectView[0], undefined);
effectBuffer.resize(4);
assertEq(effectView[0], 0);
if (typeof detachArrayBuffer === "function") {
  var detached = new Uint8Array(4);
  convertingStore(detached, { valueOf() { detachArrayBuffer(detached.buffer); return 83; } });
  assertEq(detached[0], undefined);
  assertEq(readAfterCall(detached, noEffect), undefined);
}

// Singleton views must not embed RAB data or lengths as constants.
var singletonBuffer = new ArrayBuffer(4, { maxByteLength: 8 });
var singletonView = new Uint8Array(singletonBuffer);
var singletonRead = Function("return singletonView[0];");
for (var i = 0; i < 2000; i++)
  assertEq(singletonRead(), 0);
singletonBuffer.resize(0);
assertEq(singletonRead(), undefined);
singletonBuffer.resize(4);
assertEq(singletonRead(), 0);

if (typeof reportCompare === "function")
  reportCompare(true, true);
