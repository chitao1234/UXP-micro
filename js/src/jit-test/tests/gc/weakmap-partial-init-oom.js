// |jit-test| skip-if: !this.oomTest
// OOM can interrupt either table's initialization. Unpublished Data must use
// GC-managed destruction, including its weak-list links and nursery edges.
oomTest(function() {
    let map = new WeakMap();
    let key = {};
    let symbol = Symbol();
    map.set(key, {text: "object value"});
    map.set(symbol, {text: "symbol value"});
    assertEq(map.get(key).text, "object value");
    assertEq(map.get(symbol).text, "symbol value");
});
gc();
