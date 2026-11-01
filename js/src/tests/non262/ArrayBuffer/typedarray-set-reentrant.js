// |reftest| skip-if(!xulRuntime.shell)
// Any copyright is dedicated to the Public Domain.
// http://creativecommons.org/publicdomain/zero/1.0/

// A source length/element getter or numeric conversion may invalidate the
// destination. Exercise both direct dense copies and the generic copy loop.
var constructors = [Int8Array, Uint8Array, Uint8ClampedArray,
                    Int16Array, Uint16Array, Int32Array, Uint32Array,
                    Float32Array, Float64Array];
if (typeof BigInt64Array !== "undefined")
    constructors.push(BigInt64Array, BigUint64Array);

// The public method uses native stores. Exercise the retained self-hosted
// implementation too, so its conversion and invalid-write behavior agrees.
var selfHostedSet = getSelfHostedValue("TypedArraySet");
var setters = [
    (target, source, offset) => target.set(source, offset),
    (target, source, offset) => selfHostedSet.call(target, source, offset),
];

for (let set of setters) {
for (let C of constructors) {
    let bigint = C === this.BigInt64Array || C === this.BigUint64Array;
    let value = n => bigint ? BigInt(n) : n;
    let bytes = C.BYTES_PER_ELEMENT;
    let makeBuffer = () => new ArrayBuffer(8 * bytes);

    // Dense numeric properties must not bypass validation after length access.
    {
        let buffer = makeBuffer();
        let target = new C(buffer);
        let source = {0: value(17), 1: value(42), get length() {
            detachArrayBuffer(buffer);
            return 2;
        }};
        assertEq(set(target, source, 3), undefined);
        assertEq(target.length, 0);
    }

    // ToLength can also detach while converting the length property's value.
    {
        let buffer = makeBuffer();
        let target = new C(buffer);
        let source = {0: value(1), length: {valueOf() {
            detachArrayBuffer(buffer);
            return 1;
        }}};
        assertEq(set(target, source, 3), undefined);
        assertEq(target.length, 0);
    }

    // Detachment in any source callback must preserve all subsequent reads and
    // conversions. Later exceptions, including wrong Number/BigInt content
    // types, must still propagate even though no destination writes remain.
    for (let phase of ["length", "length-conversion", "element", "conversion"]) {
    for (let failure of ["none", "getter-sentinel", "conversion-sentinel", "content-type"]) {
        let buffer = makeBuffer();
        let target = new C(buffer);
        let seen = "";
        let sentinel = {};
        let source = {
            get length() {
                seen += "L";
                if (phase === "length")
                    detachArrayBuffer(buffer);
                if (phase === "length-conversion") {
                    return {valueOf() {
                        seen += "N";
                        detachArrayBuffer(buffer);
                        return 3;
                    }};
                }
                return 3;
            },
        };
        for (let i = 0; i < 3; i++) {
            Object.defineProperty(source, i, {get() {
                seen += "G" + i;
                if (i === 2 && failure === "getter-sentinel")
                    throw sentinel;
                if (phase === "element" && i === 0)
                    detachArrayBuffer(buffer);
                return {valueOf() {
                    seen += "V" + i;
                    if (phase === "conversion" && i === 0)
                        detachArrayBuffer(buffer);
                    if (i === 2) {
                        if (failure === "conversion-sentinel")
                            throw sentinel;
                        if (failure === "content-type")
                            return bigint ? 1 : BigInt(1);
                    }
                    return value(i + 1);
                }};
            }});
        }
        let result;
        let caught;
        try { result = set(target, source, 3); } catch (e) { caught = e; }
        if (failure === "none") {
            assertEq(caught, undefined);
            assertEq(result, undefined);
        } else if (failure === "getter-sentinel" || failure === "conversion-sentinel") {
            assertEq(caught, sentinel);
        } else {
            assertEq(caught instanceof TypeError, true);
        }
        assertEq(seen, (phase === "length-conversion" ? "LN" : "L") +
                       "G0V0G1V1G2" + (failure === "getter-sentinel" ? "" : "V2"));
        assertEq(target.length, 0);
    }
    }

    // Detachment before the method starts remains an error, even for an empty
    // source. Validation must happen before observing the source length.
    {
        let buffer = makeBuffer();
        let target = new C(buffer);
        detachArrayBuffer(buffer);
        let source = {get length() { throw new Error("unexpected length access"); }};
        assertThrowsInstanceOf(() => set(target, source, 0), TypeError);
    }

    // An initially out-of-bounds fixed view also fails before source access.
    {
        let buffer = new ArrayBuffer(8 * bytes, {maxByteLength: 16 * bytes});
        let target = new C(buffer, bytes, 7);
        buffer.resize(4 * bytes);
        let source = {get length() { throw new Error("unexpected length access"); }};
        assertThrowsInstanceOf(() => set(target, source, 0), TypeError);
    }

    // No elements means no post-length detachment check. A source that is too
    // long still fails the original range check before accessing its elements.
    for (let length of [0, 9]) {
        let buffer = makeBuffer();
        let target = new C(buffer);
        let source = {
            get length() { detachArrayBuffer(buffer); return length; },
            get 0() { throw new Error("unexpected element access"); }
        };
        if (length === 0)
            assertEq(set(target, source), undefined);
        else
            assertThrowsInstanceOf(() => set(target, source), RangeError);
    }

    // Invalidate storage after a dense prefix, from either an element getter
    // or its conversion. Never write through the old backing pointer.
    for (let inConversion of [false, true]) {
        let buffer = makeBuffer();
        let target = new C(buffer);
        let source = [value(1), value(2), value(3)];
        let tailRead = false;
        Object.defineProperty(source, 1, {get() {
            if (inConversion)
                return {valueOf() { detachArrayBuffer(buffer); gc(); return value(2); }};
            detachArrayBuffer(buffer);
            gc();
            return value(2);
        }});
        Object.defineProperty(source, 2, {get() {
            tailRead = true;
            return value(3);
        }});
        assertEq(set(target, source, 3), undefined);
        assertEq(tailRead, true);
        assertEq(target.length, 0);
    }

    // Shrinking in the length getter invalidates the original destination
    // range. Cover tracking views, fixed views, and views with a byte offset.
    for (let fixed of [false, true]) {
        for (let viewOffset of [0, 1]) {
            let buffer = new ArrayBuffer(8 * bytes, {maxByteLength: 16 * bytes});
            let target = fixed ? new C(buffer, viewOffset * bytes, 7) :
                                 new C(buffer, viewOffset * bytes);
            let source = {0: value(11), 1: value(12), get length() {
                buffer.resize(4 * bytes);
                return 2;
            }};
            assertEq(set(target, source, 3), undefined);
            if (!fixed && viewOffset === 0)
                assertEq(target[3], value(11));
            buffer.resize(8 * bytes);
            assertEq(target[4], value(0));
        }
    }

    // Do not shorten the source loop when a callback temporarily shrinks the
    // buffer. Every getter/conversion must run, even for skipped writes.
    for (let fixed of [false, true]) {
        let buffer = new ArrayBuffer(8 * bytes, {maxByteLength: 16 * bytes});
        let target = fixed ? new C(buffer, 0, 8) : new C(buffer);
        let seen = "";
        let source = [value(10), {valueOf() {
            seen += "V";
            buffer.resize(4 * bytes);
            return value(11);
        }}, value(12), value(13)];
        Object.defineProperty(source, 2, {get() {
            seen += "G";
            buffer.resize(8 * bytes);
            gc();
            return value(12);
        }});
        assertEq(set(target, source, 3), undefined);
        assertEq(seen, "VG");
        assertEq(target[3], value(10));
        assertEq(target[4], value(0));
        assertEq(target[5], value(12));
        assertEq(target[6], value(13));
    }

    // Shared growth and overlapping typed-array copies keep working.
    if (typeof SharedArrayBuffer !== "undefined") {
        let buffer = new SharedArrayBuffer(8 * bytes, {maxByteLength: 16 * bytes});
        let target = new C(buffer);
        let source = {0: value(21), 1: value(22), get length() {
            buffer.grow(16 * bytes);
            return 2;
        }};
        assertEq(set(target, source, 3), undefined);
        assertEq(target[3], value(21));
        assertEq(target[4], value(22));
        set(target, target.subarray(3, 5), 4);
        assertEq(target[4], value(21));
        assertEq(target[5], value(22));
    }
}
}

if (typeof reportCompare === "function")
    reportCompare(true, true);
