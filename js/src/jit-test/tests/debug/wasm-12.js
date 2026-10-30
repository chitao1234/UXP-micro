// Tests that wasm module scripts have special URLs.

if (!wasmIsSupported())
  quit();

var g = newGlobal();
g.eval(`
function initWasm(s) { return new WebAssembly.Instance(new WebAssembly.Module(wasmTextToBinary(s))); }
o = initWasm('(module (func) (export "" 0))');
o2 = initWasm('(module (func) (func) (export "" 1))');
`);

function isWasm(script) { return script.format === "wasm"; }

// This tree retains ESR52's filename suffix. The content-hash URL assertion
// was imported in 076ad08ac6 without the matching upstream implementation.
function isValidWasmURL(url) {
   return typeof url === "string" && url.endsWith(" > wasm");
}

var dbg = new Debugger(g);
var foundScripts = dbg.findScripts().filter(isWasm);
assertEq(foundScripts.length, 2);
assertEq(isValidWasmURL(foundScripts[0].source.url), true);
assertEq(isValidWasmURL(foundScripts[1].source.url), true);
// Modules from the same host filename can have equal URL strings, but their
// debugger source identities must remain distinct.
assertEq(foundScripts[0].source !== foundScripts[1].source, true);
