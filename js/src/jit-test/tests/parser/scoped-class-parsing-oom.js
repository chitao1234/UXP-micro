// |jit-test| --no-ion
// Inject parser/emitter/first-use OOM independently of the Ion compiler's own
// allocation-failure sweep. Ion execution has separate coverage in the main
// scoped-class test and class-static-initializer-receiver.js.
let prior = scopedClassParsing();
try {
    scopedClassParsing(true);
    if (typeof oomTest === 'function') {
        oomTest(() => {
            assertEq(evaluate('(function(){class C { x=42; } '+
                'function following(){return new C().x;}return following();})();'),42);
        });
    }
} finally {
    scopedClassParsing(prior);
}
