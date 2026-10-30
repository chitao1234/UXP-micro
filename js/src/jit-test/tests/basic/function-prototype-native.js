load(libdir + "asserts.js");

function checkPrototype(realm) {
    const fp = realm.Function.prototype;
    assertEq(typeof fp, "function");
    assertEq(Object.getPrototypeOf(fp), realm.Object.prototype);
    assertEq(fp.name, "");
    assertEq(fp.length, 0);
    assertEq(Object.prototype.hasOwnProperty.call(fp, "prototype"), false);
    assertEq(fp(), undefined);
    assertEq(fp.call(null, 1, 2), undefined);
    assertEq(fp.apply({}, [1, 2]), undefined);
    const bound = fp.bind(null, 1);
    assertEq(bound(), undefined);
    assertThrowsInstanceOf(() => new fp(), realm.TypeError);
    assertThrowsInstanceOf(() => new bound(), realm.TypeError);
    assertThrowsInstanceOf(() => realm.Reflect.construct(fp, []), realm.TypeError);

    for (const property of ["name", "length"]) {
        const descriptor = Object.getOwnPropertyDescriptor(fp, property);
        assertEq(descriptor.value, property == "name" ? "" : 0);
        assertEq(descriptor.writable, false);
        assertEq(descriptor.enumerable, false);
        assertEq(descriptor.configurable, true);
        assertEq(delete fp[property], true);
        assertEq(Object.prototype.hasOwnProperty.call(fp, property), false);
        assertEq(fp[property], undefined);
        Object.defineProperty(fp, property, descriptor);
        assertEq(fp[property], descriptor.value);
    }

    for (const property of ["caller", "arguments"]) {
        assertThrowsInstanceOf(() => fp[property], realm.TypeError);
        assertThrowsInstanceOf(() => { fp[property] = null; }, realm.TypeError);
        const descriptor = Object.getOwnPropertyDescriptor(fp, property);
        assertEq(typeof descriptor.get, "function");
        assertEq(typeof descriptor.set, "function");
        assertEq(descriptor.enumerable, false);
        assertEq(descriptor.configurable, true);
        assertThrowsInstanceOf(() => descriptor.get.call(fp), realm.TypeError);
        assertThrowsInstanceOf(() => descriptor.set.call(fp, null), realm.TypeError);
    }

    // Bootstrap also creates %ThrowTypeError%. It must remain a nonextensible
    // native function using this realm's Function.prototype.
    const args = realm.Function("'use strict'; return arguments;")();
    const callee = Object.getOwnPropertyDescriptor(args, "callee");
    assertEq(callee.get, callee.set);
    assertEq(Object.getPrototypeOf(callee.get), fp);
    assertEq(Object.isExtensible(callee.get), false);
    assertEq(Object.getOwnPropertyDescriptor(callee.get, "length").value, 0);
    assertEq(Object.getOwnPropertyDescriptor(callee.get, "length").configurable, false);
    assertThrowsInstanceOf(() => callee.get(), realm.TypeError);

    const nativeText = "function () {\n    [native code]\n}";
    assertEq(fp.toString(), nativeText);
    if (fp.toSource)
        assertEq(fp.toSource(), nativeText);
}

checkPrototype(this);
for (let i = 0; i < 4; i++) {
    const realm = newGlobal();
    checkPrototype(realm);
    gc();
    assertEq(realm.Function.prototype(), undefined);
}

// Warm normal closures and native calls, then change an inherited function
// property across type categories. The older clone/type machinery still needs
// Function.prototype's default new group to have unknown properties.
function makeClosure(value) {
    return function closure() { return value; };
}
function invokeNative(fp, value) {
    assertEq(fp(value), undefined);
    return fp.nativePrototypeTest;
}
try {
    for (const marker of [1, { marker: true }, "string"]) {
        Function.prototype.nativePrototypeTest = marker;
        for (let i = 0; i < 1000; i++) {
            const closure = makeClosure(i);
            assertEq(closure(), i);
            assertEq(closure.nativePrototypeTest, marker);
            assertEq(invokeNative(Function.prototype, i), marker);
        }
        gc();
    }
} finally {
    delete Function.prototype.nativePrototypeTest;
}
