// |jit-test| --ion-offthread-compile=off

// Exercise code retirement while sibling functions, optimized ICs, debugger
// hooks and incremental/compacting GC continue to use executable pools.
jitCodeDecommit(true);
var global = newGlobal();
var debuggerObject = new Debugger(global);
var kept = [];
for (var round = 0; round < 8; round++) {
    var functions = [];
    for (var i = 0; i < 90; i++) {
        var f = global.eval('(function f' + round + '_' + i + '(o,n) {' +
            'var sum=0;for(var j=0;j<n;j++)sum+=o.x+j;return sum+' + i + ';})');
        for (var hot = 0; hot < 40; hot++)
            assertEq(f({x: 3}, 50), 1375 + i);
        functions.push(f);
    }
    kept.push(functions[round]);
    functions = null;
    debuggerObject.onEnterFrame = function(frame) { return undefined; };
    assertEq(kept[round]({x: 3}, 50), 1375 + round);
    debuggerObject.onEnterFrame = undefined;
    gc();
    // Toggle policy with already decommitted pools still available for tail
    // allocation. Turning the policy off must not lose their backing state.
    jitCodeDecommit(round % 2 == 0);
    for (var k = 0; k < kept.length; k++)
        assertEq(kept[k]({x: 3}, 50), 1375 + k);
    if (round % 2)
        gc(global, 'shrinking');
}
jitCodeDecommit(true);
gcslice(1);
for (var i = 0; i < kept.length; i++)
    assertEq(kept[i]({x: 3}, 50), 1375 + i);
while (gcstate() !== 'NotActive')
    gcslice(10000);
gc(global, 'shrinking');
for (var i = 0; i < kept.length; i++)
    assertEq(kept[i]({x: 3}, 50), 1375 + i);
