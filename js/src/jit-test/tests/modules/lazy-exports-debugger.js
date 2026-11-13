let prior=lazyModuleExports();
try {
    lazyModuleExports(true);
    let g=newGlobal();
    let source='export const value=40;'+
        'export function after(x){return value+x;}';
    g.eval('var module=parseModule('+JSON.stringify(source)+');'+
           'module.declarationInstantiation();module.evaluation();'+
           'var f=getModuleEnvironmentValue(module,"after");'+
           'assertEq(isLazyFunction(f),true);');
    let dbg=new Debugger;
    let global=dbg.addDebuggee(g);
    let fun=global.getOwnPropertyDescriptor('f').value;
    let script=fun.script;
    assertEq(script instanceof Debugger.Script,true);
    assertEq(script.source.text,source);
    assertEq(script.getChildScripts().length,0);
    assertEq(fun.call(null,2).return,42);
    assertEq(g.eval('f.toString()'),'function after(x){return value+x;}');
    gc();
    assertEq(fun.call(null,3).return,43);
    dbg.removeAllDebuggees();
} finally {
    lazyModuleExports(prior);
}
