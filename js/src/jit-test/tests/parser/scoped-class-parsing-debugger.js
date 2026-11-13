let prior=scopedClassParsing();
try {
    scopedClassParsing(true);
    let g=newGlobal();
    let source='class C { x=40; static { this.value=1; } } '+
        'function after(v){return ()=>new C().x+v;} var f=after;';
    g.eval(source);
    assertEq(g.eval('isLazyFunction(f)'),true);
    let dbg=new Debugger;
    let global=dbg.addDebuggee(g);
    let fun=global.getOwnPropertyDescriptor('f').value;
    assertEq(fun.script instanceof Debugger.Script,true);
    assertEq(fun.script.source.text,source);
    assertEq(g.eval('f.toString()'),'function after(v){return ()=>new C().x+v;}');
    let closure=fun.call(null,2).return;
    assertEq(closure.call(null).return,42);
    gc();
    assertEq(closure.call(null).return,42);
    dbg.removeAllDebuggees();
} finally {
    scopedClassParsing(prior);
}
