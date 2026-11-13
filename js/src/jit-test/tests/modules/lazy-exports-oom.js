// |jit-test| --no-ion
// Isolate front-end/first-use OOM from Ion's own allocation sweep.
let prior=lazyModuleExports();
try {
    lazyModuleExports(true);
    if (typeof oomTest==='function') {
        oomTest(()=>{
            let m=parseModule('export const value=40;'+
                'export function read(){return value+2;}'+
                'export class C { x=value; }');
            m.declarationInstantiation();
            m.evaluation();
            assertEq(getModuleEnvironmentValue(m,'read')(),42);
        });
    }
} finally {
    lazyModuleExports(prior);
}
