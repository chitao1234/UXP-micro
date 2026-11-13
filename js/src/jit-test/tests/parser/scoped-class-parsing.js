// The class body retains its existing full parse. Later functions stay lazy.
load(libdir+'asserts.js');
let prior = scopedClassParsing();
let priorModules = lazyModuleExports();
try {
    for (let enabled of [false, true, false, true]) {
        scopedClassParsing(enabled);
        for (let classSource of [
            'class C { x=42; }',
            'class C { static x=42; }',
            'class C { static { this.x=42; } }',
            'class C { ["x"]=()=>42; }',
            'class C { x=class D { y=42; }; }',
        ]) {
            let g = newGlobal();
            evaluate('function before(){return 21;} '+classSource+
                     ' function after(x){return before()+x;}', {global:g});
            assertEq(g.eval('isLazyFunction(before)'), true);
            assertEq(g.eval('isLazyFunction(after)'), enabled);
            assertEq(g.eval('after(21)'), 42);
            gc();
            assertEq(g.eval('after(22)'), 43);
        }
        let g = newGlobal();
        evaluate('function enclosing(v){class C { x=v; }'+
                 'function nested(){return new C().x;}return nested;}'+
                 'function unrelated(){return 19;}var f=enclosing(42);', {global:g});
        assertEq(g.eval('f()'),42);
        assertEq(g.eval('isLazyFunction(unrelated)'),enabled);
        gc();
        assertEq(g.eval('f()'),42);
    }
    scopedClassParsing(true);
    let eager=newGlobal({disableLazyParsing:true});
    eager.eval('class C { x=42; } function after(){return new C().x;}'+
        'assertEq(isLazyFunction(after),false);assertEq(after(),42);');
    for (let source of [
        'class C{x=1;}function after(){let a;let a;}',
        'class C{static{this.x=1;}}function after(){return ()=>{let a;let a;};}',
    ]) {
        let invalid=newGlobal();
        assertThrowsInstanceOf(()=>evaluate(source,{global:invalid}),invalid.SyntaxError);
    }
    if (helperThreadCount() > 0) {
        offThreadCompileScript('class OffThreadClass { x=42; } '+
                               'function offThreadLater(){return new OffThreadClass().x;} '+
                               'assertEq(isLazyFunction(offThreadLater),true);'+
                               'assertEq(offThreadLater(),42);');
        runOffThreadScript();
    }
    let module = parseModule('class ModuleClass { x=42; } '+
                             'function moduleLater(){return new ModuleClass().x;} '+
                             'assertEq(isLazyFunction(moduleLater),true);'+
                             'assertEq(moduleLater(),42);export {moduleLater};');
    module.declarationInstantiation();
    module.evaluation();
    // Before lazy exports were enabled, an earlier export masked the class
    // fallback. Check all four policies, including null-entry preservation.
    for (let exports of [false,true]) {
        for (let classes of [false,true]) {
            lazyModuleExports(exports);
            scopedClassParsing(classes);
            for (let source of [
                'class C { x=1; }',
                'const C=class { x=1; };',
                'class C { static x=1; }',
                'class C { static { this.x=1; } }',
                'class C { x=class D { y=1; }; }',
                'const C=(()=>class { x=1; })();',
            ]) {
                let m=parseModule('export const value=40;'+source+
                    'function after(){return value+2;}'+
                    'export const lazy=isLazyFunction(after);export {after};');
                m.declarationInstantiation();m.evaluation();
                assertEq(getModuleEnvironmentValue(m,'lazy'),exports&&classes);
                gc();
                assertEq(getModuleEnvironmentValue(m,'after')(),42);
            }
        }
    }
} finally {
    scopedClassParsing(prior);
    lazyModuleExports(priorModules);
}
