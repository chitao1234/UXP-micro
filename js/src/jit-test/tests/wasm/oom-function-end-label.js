// |jit-test| --wasm-always-baseline; skip-if: !wasmIsSupported() || !this.oomTest
load(libdir + "wasm.js");

// The initial control frame must never be published with a null exit label
// when the baseline compiler's label-pool allocation fails, including on a
// compilation helper thread.
oomTest(function() {
    var f = wasmEvalText('(module (func (result i32) (i32.const 42)) (export "f" 0))');
    assertEq(f.exports.f(), 42);
});
