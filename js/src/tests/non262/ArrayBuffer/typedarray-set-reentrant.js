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
        assertThrowsInstanceOf(() => target.set(source, 3), TypeError);
    }

    // ToLength can also detach while converting the length property's value.
    {
        let buffer = makeBuffer();
        let target = new C(buffer);
        let source = {0: value(1), length: {valueOf() {
            detachArrayBuffer(buffer);
            return 1;
        }}};
        assertThrowsInstanceOf(() => target.set(source, 3), TypeError);
    }

    // Preserve observable ordering: read and convert the first element before
    // reporting detachment, and propagate an exception from conversion.
    for (let throws of [false, true]) {
        let buffer = makeBuffer();
        let target = new C(buffer);
        let seen = "";
        let sentinel = {};
        let source = {
            get length() { seen += "L"; detachArrayBuffer(buffer); return 1; },
            get 0() {
                seen += "G";
                return {valueOf() {
                    seen += "V";
                    if (throws)
                        throw sentinel;
                    return value(1);
                }};
            }
        };
        let caught;
        try { target.set(source, 3); } catch (e) { caught = e; }
        assertEq(throws ? caught === sentinel : caught instanceof TypeError, true);
        assertEq(seen, "LGV");
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
            assertEq(target.set(source), undefined);
        else
            assertThrowsInstanceOf(() => target.set(source), RangeError);
    }

    // Invalidate storage after a dense prefix, from either an element getter
    // or its conversion. Never write through the old backing pointer.
    for (let inConversion of [false, true]) {
        let buffer = makeBuffer();
        let target = new C(buffer);
        let source = [value(1), value(2)];
        Object.defineProperty(source, 1, {get() {
            if (inConversion)
                return {valueOf() { detachArrayBuffer(buffer); gc(); return value(2); }};
            detachArrayBuffer(buffer);
            gc();
            return value(2);
        }});
        assertThrowsInstanceOf(() => target.set(source, 3), TypeError);
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
            assertEq(target.set(source, 3), undefined);
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
        assertEq(target.set(source, 3), undefined);
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
        assertEq(target.set(source, 3), undefined);
        assertEq(target[3], value(21));
        assertEq(target[4], value(22));
        target.set(target.subarray(3, 5), 4);
        assertEq(target[4], value(21));
        assertEq(target[5], value(22));
    }
}

if (typeof reportCompare === "function")
    reportCompare(true, true);
