// An async generator's request queue must be a private FIFO, release consumed
// links, and safely reuse its cached request after the queue becomes empty.
async function* values() {
    for (let i = 0; i < 4096; i++)
        yield i;
}
let generator = values();
let completed = 0;
let failure;
for (let i = 0; i < 4096; i++) {
    generator.next().then(result => {
        assertEq(result.value, i);
        assertEq(result.done, false);
        assertEq(completed++, i);
    }).catch(error => { failure = error; });
}
gc();
drainJobQueue();
assertEq(failure, undefined);
assertEq(completed, 4096);
for (let i = 0; i < 100; i++) {
    let done = false;
    generator.next().then(result => {
        assertEq(result.done, true);
        done = true;
    });
    drainJobQueue();
    assertEq(done, true);
}

async function* commands() { yield 1; yield 2; }
let g = commands();
let order = [];
g.next().then(r => order.push(r.value));
g.return(7).then(r => { assertEq(r.done, true); order.push(r.value); });
g.throw("rejected").catch(error => order.push(error));
g.next().then(r => { assertEq(r.done, true); order.push(r.value); });
drainJobQueue();
assertEq(JSON.stringify(order), '[1,7,"rejected",null]');

