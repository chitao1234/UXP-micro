// Ordinary string cells must survive every rooting and JIT store path. Run with
// the interpreter, Baseline, and Ion eager; minor collections are explicit.
if (typeof nurseryStrings !== "function")
    quit();

var original = nurseryStrings();
nurseryStrings(true, false);
var before = nurseryStrings();

// The generated String-object path must not keep its length in a register
// used as post-barrier scratch. Exercise both nursery and tenured inputs.
function boxedStringLength(s) { return new String(s).length; }
for (var i = 0; i < 300; i++) {
    var boxedInput = JSON.parse('"box-' + i + '"');
    if (i % 3 === 0) minorgc();
    assertEq(boxedStringLength(boxedInput), boxedInput.length);
}

// An eval guard can be live while minor GC removes other weak cache keys.
function evaluateNurserySource(source) { return eval(source); }
for (var i = 0; i < 100; i++) {
    var evalSource = JSON.parse('"minorgc(); ' + (1000 + i) + '"');
    assertEq(evaluateNurserySource(evalSource), 1000 + i);
}

// Copied cross-compartment strings do not retain the source cell. Keep the
// copy alive while collecting its weak wrapper-cache key.
var stringRealm = newGlobal();
for (var i = 0; i < 100; i++) {
    var sourceString = JSON.parse('"cross-realm-' + i + '-\\u1234"');
    stringRealm.copied = sourceString;
    sourceString = null;
    minorgc();
    assertEq(stringRealm.copied, 'cross-realm-' + i + '-\u1234');
}
var probe = JSON.parse('"ordinary-nursery-allocation-probe"');
assertEq(isNurseryAllocated(probe), true);
minorgc();
assertEq(isNurseryAllocated(probe), false);
var live = [];
function dynamic(i, wide) {
    // JSON parsing allocates fresh linear cells rather than returning atoms or
    // constructing only dependent strings with shared source buffers.
    return JSON.parse('"ordinary-' + (wide ? '\\u1234\\ud800' : 'latin1') + '-' + (i % 41) + '"');
}
function make(i) {
    var s = dynamic(i, i % 2);
    return {text:s, rope:s + dynamic(i + 1, false), part:s.slice(1), array:[s]};
}
for (var round = 0; round < 16; round++) {
    for (var i = 0; i < 2000; i++) {
        var o = make(i);
        if (i % 17 === 0)
            live.push(o);
    }
    minorgc();
}
assertEq(nurseryStrings().allocated > before.allocated, true);
assertEq(nurseryStrings().promoted > before.promoted, true);
for (var o of live) {
    assertEq(o.array[0], o.text);
    assertEq(o.part, o.text.slice(1));
    assertEq(o.rope.indexOf(o.text), 0);
}

// Explicitly exercise writes from compiled code into already tenured owners.
gc();
var owners = Array.from({length:2000}, () => ({text:"", array:[""]}));
gc();
function store(owner, i) {
    owner.text = dynamic(i, true);
    owner.array[0] = dynamic(i, false);
}
for (var r = 0; r < 20; r++) {
    for (var i = 0; i < owners.length; i++)
        store(owners[i], i);
    minorgc();
    for (var i = 0; i < owners.length; i++) {
        assertEq(owners[i].text, dynamic(i, true));
        assertEq(owners[i].array[0], dynamic(i, false));
    }
}

// Tenured dependent -> dependent -> nursery base, including flattening and
// undepending. Character storage must remain valid regardless of trace order.
for (var r = 0; r < 12; r++) {
    var base = dynamic(r, true).repeat(100);
    var dep1 = base.slice(2, -2);
    gc();
    var dep2 = dep1.slice(3, -3);
    var rope = base + dep2 + base;
    var expected = JSON.stringify([base, dep1, dep2, rope]);
    minorgc();
    gc(null, "shrinking");
    assertEq(JSON.stringify([base, dep1, dep2, rope]), expected);
}

// Bulk element copies have a range barrier separate from HeapSlot::set.
// Splice and suspended generator stacks both use this path with tenured arrays.
var bulk = Array.from({length:800}, () => "");
gc();
for (var r = 0; r < 30; r++) {
    var strings = [];
    for (var i = 0; i < 400; i++) strings.push(dynamic(i + r, true));
    bulk.splice.apply(bulk, [0, 400].concat(strings));
    strings = null;
    minorgc();
    for (var i = 0; i < 400; i++) assertEq(bulk[i], dynamic(i + r, true));
}
function* suspendedString() {
    for (var i = 0; i < 100; i++) {
        var str = dynamic(i, true);
        yield 1;
        assertEq(str, dynamic(i, true));
    }
}
var suspended = suspendedString();
while (!suspended.next().done) minorgc();

// Compiled regexp stubs update malloc-backed RegExpStatics fields, which need
// post-barriers even though they are outside the GC cell and its slot arrays.
for (var r = 0; r < 40; r++) {
    var input = dynamic(r, true);
    assertEq(/ordinary-(.*)-([0-9]+)/.test(input), true);
    minorgc();
    assertEq(RegExp.input, input);
    assertEq(RegExp.lastMatch, input);
}

// Collisions and unequal encodings must not equate different strings. The
// table also has a hard entry/work cap; exceeding it must preserve all values.
nurseryStrings(true, true);
var dedupBefore = nurseryStrings();
var duplicates = [], expected = [];
for (var i = 0; i < 20000; i++) {
    var s = dynamic(i, i % 2);
    duplicates.push(s);
    expected.push('ordinary-' + (i % 2 ? '\u1234\ud800' : 'latin1') + '-' + (i % 41));
}
minorgc();
for (var i = 0; i < duplicates.length; i++)
    assertEq(duplicates[i], expected[i]);
assertEq(nurseryStrings().deduplicated > dedupBefore.deduplicated, true);

// A live policy change must not erase cells or leave stale JIT allocation
// decisions: previously compiled functions run across all transitions.
for (var mode of [false, true, false, true]) {
    nurseryStrings(mode, mode);
    for (var i = 0; i < owners.length; i++)
        store(owners[i], i);
    minorgc();
    for (var i = 0; i < owners.length; i++)
        assertEq(owners[i].text, dynamic(i, true));
}

// Major-GC slices can run between nursery allocations. Pre-barriers must skip
// young cells and all edges must be promoted before the next major slice.
var canRunMajorSlices = typeof jitTestMissingCapabilities === "undefined"
    ? typeof gcslice === "function"
    : !jitTestMissingCapabilities.gcslice;
if (canRunMajorSlices) {
    for (var r = 0; r < 8; r++) {
        startgc(1);
        for (var i = 0; i < 400; i++)
            store(owners[i], i + r);
        minorgc();
        gcslice(10);
        abortgc();
        for (var i = 0; i < 400; i++)
            assertEq(owners[i].text, dynamic(i + r, true));
    }
}
gc(null, "shrinking");
nurseryStrings(original.enabled, false);
