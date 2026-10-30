// Embedding-private module values can be non-atomized strings. Ion must read
// that value from the rooted script at execution time, including inlined calls.
let expected = ["private", "module", String(Math.random())].join("/");
let target = parseModule("export const value = 42;");
target.declarationInstantiation();
target.evaluation();
setModuleResolveHook(() => target);
let calls = 0;
setModuleDynamicImportHook((privateValue, specifier, promise) => {
    assertEq(privateValue, expected);
    assertEq(specifier, "target");
    calls++;
    finishDynamicModuleImport(privateValue, specifier, promise);
});
let module = parseModule('export function load() { return import("target"); }');
setModulePrivate(module, expected);
module.declarationInstantiation();
module.evaluation();
let load = getModuleEnvironmentValue(module, "load");
setJitCompilerOption("ion.warmup.trigger", 20);
let completed = 0;
let failure;
for (let i = 0; i < 100; i++) {
    load().then(ns => { assertEq(ns.value, 42); completed++; })
          .catch(error => { failure = error; });
    if (i % 13 === 0)
        gc();
}
drainJobQueue();
assertEq(failure, undefined);
assertEq(calls, 100);
assertEq(completed, 100);
