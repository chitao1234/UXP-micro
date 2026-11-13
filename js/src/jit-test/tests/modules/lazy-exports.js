// Module export nodes remain eager; their function bodies need not be.
load(libdir+'asserts.js');
let prior = lazyModuleExports();
try {
    for (let enabled of [false,true,false,true]) {
        lazyModuleExports(enabled);
        for (let prefix of [
            'export const marker=1;',
            'export let marker=1;',
            'export var marker=1;',
            'export function marker(){return 1;}',
            'export default function marker(){return 1;}',
            'let marker=1;export {marker};',
            'export class Marker { x=1; }',
            'export default class Marker { static { this.x=1; } }',
            'export const marker=class { x=1; };',
            'export const {marker}={marker:1};',
        ]) {
            let m=parseModule(prefix+' function after(x){return x+41;} '+
                              'export var wasLazy=isLazyFunction(after);export {after};');
            m.declarationInstantiation();
            m.evaluation();
            assertEq(getModuleEnvironmentValue(m,'wasLazy'),enabled);
            assertEq(getModuleEnvironmentValue(m,'after')(1),42);
            gc();
            assertEq(getModuleEnvironmentValue(m,'after')(2),43);
        }

        let modules=Object.create(null);
        setModuleResolveHook((ref,name)=>modules[name]);
        modules.dep=parseModule('export let value=1;export function inc(){value++;}');
        let m=parseModule('import {value,inc} from "dep";'+
                         'export function read(){return value;}'+
                         'export async function readAsync(){return read();}'+
                         'export function* values(){yield read();}'+
                         'export const before=value;inc();export const after=value;');
        m.declarationInstantiation();
        m.evaluation();
        assertEq(getModuleEnvironmentValue(m,'before'),1);
        assertEq(getModuleEnvironmentValue(m,'after'),2);
        assertEq(getModuleEnvironmentValue(m,'read')(),2);
        assertEq(getModuleEnvironmentValue(m,'values')().next().value,2);
        let readResult;
        getModuleEnvironmentValue(m,'readAsync')().then(v=>readResult=v);
        drainJobQueue();
        assertEq(readResult,2);

        let closures=parseModule('export let value=40;'+
            'export function make(x){return ()=>value+x;}'+
            'export class C { x=value; m(){return this.x;} }'+
            'export function later(){return new C().m()+2;}');
        closures.declarationInstantiation();closures.evaluation();
        let f=getModuleEnvironmentValue(closures,'make')(2);
        assertEq(f(),42);
        assertEq(getModuleEnvironmentValue(closures,'later')(),42);
        gc();assertEq(f(),42);

        // Errors must be rejected during the initial parse, even if the
        // enclosing exported function will never be called.
        for (let source of [
            'export function f(){let x;let x;}',
            'export function f(){return (()=>{let x;let x;});}',
            'export const x=1;export {x};',
            'export {missing};',
            'function outer(){export const invalid=1;}',
            'export class C { static { return; } }',
        ]) {
            assertThrowsInstanceOf(()=>parseModule(source),SyntaxError);
        }
    }
    lazyModuleExports(true);
    // Parsing a malformed module cannot poison a subsequent parser.
    try { parseModule('export function invalid( {'); } catch(e) {}
    let m=parseModule('export const x=1;function untouched(){return x;}'+
                      'export var lazy=isLazyFunction(untouched);');
    m.declarationInstantiation();m.evaluation();
    assertEq(getModuleEnvironmentValue(m,'lazy'),true);
    // Restoring the export parser must not override the compartment's eager
    // compilation policy, and changing policy cannot invalidate older lazies.
    let eagerGlobal=newGlobal({disableLazyParsing:true});
    eagerGlobal.eval('var m=parseModule("export const x=40;export function f(){return x+2;}");'+
        'm.declarationInstantiation();m.evaluation();'+
        'var f=getModuleEnvironmentValue(m,"f");'+
        'assertEq(isLazyFunction(f),false);assertEq(f(),42);');
    lazyModuleExports(false);
    gc();
    assertEq(getModuleEnvironmentValue(m,'untouched')(),1);
    lazyModuleExports(true);
    if (helperThreadCount()>0) {
        offThreadCompileModule('export const x=40;'+
            'export function helper(){return x+2;}'+
            'export var lazy=isLazyFunction(helper);');
        let helper=finishOffThreadModule();
        helper.declarationInstantiation();helper.evaluation();
        assertEq(getModuleEnvironmentValue(helper,'lazy'),true);
        assertEq(getModuleEnvironmentValue(helper,'helper')(),42);
    }
    let awaited=parseModule('export let value=await Promise.resolve(40);'+
        'export function after(){return value+2;}');
    awaited.declarationInstantiation();
    let pending=awaited.evaluation();
    assertEq(pending instanceof Promise,true);
    drainJobQueue();
    assertEq(getModuleEnvironmentValue(awaited,'after')(),42);
} finally {
    lazyModuleExports(prior);
}
