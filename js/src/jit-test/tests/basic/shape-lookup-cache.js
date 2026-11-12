// Exercise the property/prototype and GC cases covered by Firefox 78's
// shape-teleporting and incremental-AccessorShape-barrier regressions, plus the
// bounded-cache policy and negative-key paths in this older engine.
const previousShapeCache = shapeLookupCache();
try {
    for (const enabled of [false, true, false, true]) {
        shapeLookupCache(enabled);
        const objects = [];
        for (let n = 0; n < 120; n++) {
            const obj = {};
            for (let i = 0; i < 80; i++) obj['p' + i] = n + i;
            objects.push(obj);
        }
        for (let rep = 0; rep < 40; rep++) {
            for (let n = 0; n < objects.length; n++) {
                const obj = objects[n];
                assertEq(obj.p20, n + 20);
                assertEq(obj.absent, undefined);
                const i = rep % 80;
                assertEq(obj['p' + i], n + i);
            }
        }
        for (let n = 0; n < objects.length; n++) {
            const obj = objects[n], key = Symbol('key-' + n);
            for (let i = 0; i < 12; i++) assertEq(obj[key], undefined);
            obj[key] = n;
            assertEq(obj[key], n);
            delete obj.p25;
            assertEq(obj.p25, undefined);
            obj.p25 = n * 2;
            assertEq(obj.p25, n * 2);
            assertEq(Object.keys(obj).pop(), 'p25');
            Object.defineProperty(obj, 'p26', {get() { return n * 3; }, configurable: true});
            assertEq(obj.p26, n * 3);
            Object.setPrototypeOf(obj, {absent: n});
            assertEq(obj.absent, n);
            Object.preventExtensions(obj);
            assertEq(Reflect.set(obj, 'newProperty', 1), false);
        }
        gc();
        gc('shrinking');
        for (let n = 0; n < objects.length; n++) {
            assertEq(objects[n].p20, n + 20);
            assertEq(objects[n].p25, n * 2);
            assertEq(objects[n].p26, n * 3);
            assertEq(objects[n].absent, n);
        }
        const other = newGlobal();
        other.eval('var remote={};for(var i=0;i<80;i++)remote["r"+i]=i;');
        for (let i = 0; i < 80; i++) assertEq(other.remote['r' + i], i);
    }
} finally {
    shapeLookupCache(previousShapeCache);
}
