/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
// Shared semantic tests, invoked in Window and dedicated Worker.
async function runAbortDeadlineTests(base) {
  const result = { count: 0, fail: [], done: false };
  const unhandled = event => { result.fail.push('unexpected unhandled rejection: ' + String(event.reason)); event.preventDefault(); };
  self.addEventListener('unhandledrejection', unhandled);
  function check(value, name) { ++result.count; if (!value) result.fail.push(name); }
  function sleep(ms) { return new Promise(resolve => setTimeout(resolve, ms)); }
  function caught(promise) { return promise.then(() => ({ resolved: true }), reason => ({ reason })); }
  function bounded(promise) {
    return Promise.race([promise, sleep(3000).then(() => { throw new Error('operation timeout'); })]);
  }
  async function test(name, run) {
    try { await bounded(run()); }
    catch (e) { check(false, name + ': ' + String(e)); }
  }
  await test('reason identity', async () => {
    for (const reason of [null, false, 0, '', 'cancel', 12n, { token: 42 }, new Error('stop')]) {
      const c = new AbortController(), s = c.signal;
      check(s.reason === undefined && !s.aborted, 'initial reason');
      s.throwIfAborted();
      let events = 0;
      s.onabort = e => {
        ++events;
        check(s.aborted && s.reason === reason, 'reason installed before event');
        check(e.isTrusted && !e.bubbles && !e.cancelable, 'abort event flags');
        c.abort('reentrant');
      };
      c.abort(reason); c.abort('second');
      check(s.reason === reason && events === 1, 'first abort wins');
      let thrown = {};
      try { s.throwIfAborted(); } catch (e) { thrown = e; }
      check(thrown === reason, 'throwIfAborted exact reason');
      const stat = AbortSignal.abort(reason);
      check(stat.aborted && stat.reason === reason, 'static exact reason');
      const lazy = new AbortController(); lazy.abort(reason);
      check(lazy.signal.reason === reason, 'abort before signal getter');
    }
    for (const c of [new AbortController(), new AbortController()]) {
      c.abort(undefined);
      const s = c.signal;
      check(s.reason instanceof DOMException && s.reason.name === 'AbortError', 'default AbortError');
      check(s.reason === s.reason, 'default reason stable');
    }
    const stat = AbortSignal.abort(); let fired = false;
    stat.onabort = () => fired = true; await sleep(1);
    check(stat.reason.name === 'AbortError' && !fired, 'static abort has no deferred event');
    if (typeof window !== 'undefined' && window.audit) {
      // The standalone chrome harness performs a real GC/CC while the reason
      // is retained only by controller/signal state, then resumes this test.
      const lazy = new AbortController(); lazy.abort({ survived: 731 });
      const direct = AbortSignal.abort({ survived: 732 });
      const eventCycle = new AbortController();
      eventCycle.signal.onabort = function() { return eventCycle.signal.reason; };
      eventCycle.abort({ survived: 733 });
      window.audit.gcReady = true;
      while (!window.audit.gcDone) await sleep(5);
      check(lazy.signal.reason.survived === 731, 'lazy controller reason survives GC/CC');
      check(direct.reason.survived === 732, 'static signal reason survives GC/CC');
      check(eventCycle.signal.reason.survived === 733, 'reason/listener graph survives GC/CC');
    }
  });
  await test('deadlines', async () => {
    for (const bad of [-1, NaN, Infinity, 2 ** 64]) {
      let threw = false;
      try { AbortSignal.timeout(bad); } catch (e) { threw = e instanceof TypeError; }
      check(threw, 'timeout EnforceRange ' + bad);
    }
    const huge = AbortSignal.timeout(Number.MAX_SAFE_INTEGER);
    const zero = AbortSignal.timeout(0);
    check(!zero.aborted, 'zero timeout asynchronous');
    const done = new Promise(resolve => zero.onabort = resolve);
    await done;
    check(zero.aborted && zero.reason instanceof DOMException && zero.reason.name === 'TimeoutError', 'TimeoutError');
    check(zero.reason === zero.reason, 'timeout reason stable');
    check(!huge.aborted, 'huge timeout does not wrap');
    let order = '';
    for (const n of ['1','2','3']) AbortSignal.timeout(5).onabort = () => order += n;
    await new Promise(resolve => AbortSignal.timeout(5).onabort = resolve);
    check(order === '123', 'same deadline insertion order');
    const protectedSignal = AbortSignal.timeout(10);
    for (let n = 1; n <= 1000; ++n) clearTimeout(n);
    await new Promise(resolve => protectedSignal.onabort = resolve);
    check(protectedSignal.aborted, 'clearTimeout cannot cancel native deadline');
  });
  await test('fetch cancellation', async () => {
    for (const reason of [null, 17, { fetch: 'pre-aborted' }]) {
      const c = new AbortController(); c.abort(reason);
      const rejection = await caught(fetch(base + '?headers', { signal: c.signal }));
      check(rejection.reason === reason, 'pre-aborted fetch exact reason');
      const req = new Request(base + '?headers', { signal: c.signal });
      const clone = req.clone();
      check((await caught(fetch(clone))).reason === reason, 'cloned Request exact reason');
    }
    const c = new AbortController(), reason = { fetch: 'in-flight' };
    const observers = [];
    const options = { signal: c.signal, observe: { handleEvent(observer) { observers.push(observer); } } };
    const first = caught(fetch(base + '?headers', options));
    const second = caught(fetch(base + '?headers', options));
    await sleep(15); c.abort(reason);
    check((await first).reason === reason, 'first follower fetch');
    check((await second).reason === reason, 'second follower fetch');
    check(observers.length === 2 && observers.every(o => o.state === 'aborted'), 'self-unfollowing observers do not skip fetch followers');
    const timed = AbortSignal.timeout(10);
    const timeoutError = await caught(fetch(base + '?headers', { signal: timed }));
    check(timeoutError.reason === timed.reason && timed.reason.name === 'TimeoutError', 'deadline fetch exact reason');
  });
  await test('body cancellation', async () => {
    const c = new AbortController(), reason = { body: 42 };
    const response = await fetch(base + '?body', { signal: c.signal });
    const pending = caught(response.text());
    await sleep(10); c.abort(reason);
    check((await pending).reason === reason, 'in-flight text exact reason');
    check((await caught(response.text())).reason instanceof TypeError, 'second consume after abort is TypeError');
    const d = new AbortController();
    const later = await fetch(base + '?body', { signal: d.signal });
    d.abort(reason);
    check((await caught(later.arrayBuffer())).reason === reason, 'already aborted body');
    const e = new AbortController();
    const streamResponse = await fetch(base + '?body', { signal: e.signal });
    const savedBody = streamResponse.body;
    const reader = savedBody.getReader();
    check(!streamResponse.bodyUsed, 'locking idle stream does not disturb body');
    check((await caught(Promise.resolve().then(() => streamResponse.clone()))).reason instanceof TypeError, 'locked body clone still rejected');
    const closed = caught(reader.closed);
    e.abort(reason);
    check(!streamResponse.bodyUsed, 'erroring idle body does not disturb it');
    check((await caught(streamResponse.text())).reason instanceof TypeError, 'locked aborted body consume is TypeError');
    check((await closed).reason === reason, 'idle stream reader closed exact reason');
    await sleep(1);
    check(streamResponse.body === savedBody, 'errored body identity survives native cleanup');
    check((await caught(reader.read())).reason === reason, 'stream read exact reason');
    check(streamResponse.bodyUsed, 'rejected stream read disturbs body');
    const f = new AbortController();
    const original = await fetch(base + '?body', {signal: f.signal});
    const clone = original.clone();
    const originalText = caught(original.text()), cloneText = caught(clone.text());
    f.abort(reason);
    check((await originalText).reason === reason, 'original text after clone exact reason');
    check((await cloneText).reason === reason, 'cloned text exact reason');
    const g = new AbortController();
    const originalStream = await fetch(base + '?body', {signal: g.signal});
    const cloneStream = originalStream.clone();
    const originalReader = originalStream.body.getReader(), clonedReader = cloneStream.body.getReader();
    const originalClosed = caught(originalReader.closed), clonedClosed = caught(clonedReader.closed);
    g.abort(reason);
    check((await originalClosed).reason === reason, 'original stream after clone exact reason');
    check((await clonedClosed).reason === reason, 'cloned stream exact reason');
    const exposedController = new AbortController();
    const exposed = await fetch(base + '?body', {signal: exposedController.signal});
    const exposedBody = exposed.body;
    exposedController.abort(reason);
    check((await caught(exposed.text())).reason === reason && exposed.body === exposedBody, 'exposed unlocked errored body exact reason');
    const untouchedController = new AbortController();
    const untouched = await fetch(base + '?body', {signal: untouchedController.signal});
    const untouchedBody = untouched.body;
    untouchedController.abort(reason); await sleep(1);
    check(untouched.body === untouchedBody && !untouched.bodyUsed, 'idle unowned reader body retains error identity without disturbance');
    const untouchedReader = untouchedBody.getReader();
    const untouchedClosed = caught(untouchedReader.closed);
    check((await untouchedClosed).reason === reason && (await caught(untouchedReader.read())).reason === reason, 'reader created after abort sees retained exact reason');
    const h = new AbortController();
    const lazyOriginal = await fetch(base + '?body', {signal: h.signal});
    const lazyClone = lazyOriginal.clone();
    h.abort(reason);
    check((await caught(lazyClone.arrayBuffer())).reason === reason, 'lazy cloned body exact reason');
    const nullController = new AbortController();
    const empty = await fetch(base + '?null', {signal: nullController.signal});
    nullController.abort(reason);
    check(empty.body === null && !empty.bodyUsed, 'null response body remains unused');
    check(await empty.text() === '' && await empty.text() === '' && !empty.bodyUsed, 'null body consumption unaffected by abort');
  });
  if (typeof document !== 'undefined') {
    await test('cross realm and detach', async () => {
      const iframe = document.createElement('iframe');
      document.documentElement.appendChild(iframe);
      const other = iframe.contentWindow, reason = { realm: true };
      const c = new other.AbortController(); c.abort(reason);
      check(c.signal.reason === reason, 'cross-realm reason identity');
      const defaultController = new other.AbortController(); defaultController.abort();
      check(defaultController.signal.reason instanceof other.DOMException, 'default reason created in signal realm');
      const detachedDefault = other.AbortSignal.abort();
      const detachedExplicit = other.AbortSignal.abort(reason);
      const otherDOMException = other.DOMException;
      const signal = other.AbortSignal.timeout(20); let fired = false;
      signal.onabort = () => fired = true;
      iframe.remove(); await sleep(40);
      check(!signal.aborted && !fired, 'detached frame deadline cancelled');
      check(detachedDefault.reason instanceof otherDOMException && detachedDefault.reason.name === 'AbortError', 'unobserved default reason survives detach in original realm');
      check(detachedDefault.reason === detachedDefault.reason, 'detached default reason stable');
      let thrown;
      try { detachedDefault.throwIfAborted(); } catch (e) { thrown = e; }
      check(thrown === detachedDefault.reason, 'detached default throwIfAborted');
      try { detachedExplicit.throwIfAborted(); } catch (e) { thrown = e; }
      check(detachedExplicit.reason === reason && thrown === reason, 'detached explicit reason identity');
      const script = 'onmessage=function(){AbortSignal.timeout(30).onabort=function(){postMessage("late");};postMessage("ready");};';
      const url = URL.createObjectURL(new Blob([script], {type:'application/javascript'}));
      const worker = new Worker(url);
      await new Promise(resolve => { worker.onmessage = resolve; worker.postMessage(1); });
      let late = false; worker.onmessage = () => late = true;
      worker.terminate(); URL.revokeObjectURL(url);
      await sleep(50); check(!late, 'terminated worker deadline discarded');
      // Exercise cancellation between worker dispatch and the main-thread
      // fetch runnable, with a JS object reason that must never cross threads.
      for (let rep = 0; rep < 8; ++rep) {
        const stressSource = 'onmessage=function(e){var c=new AbortController();c.abort({reason:42});fetch(e.data,{signal:c.signal}).catch(function(){});var s=AbortSignal.timeout(50);fetch(e.data,{signal:s}).catch(function(){});postMessage("started");close();};';
        const stressURL = URL.createObjectURL(new Blob([stressSource],{type:'application/javascript'}));
        const stressWorker = new Worker(stressURL);
        await new Promise(resolve => {stressWorker.onmessage=resolve;stressWorker.postMessage(base+'?headers');});
        stressWorker.terminate();URL.revokeObjectURL(stressURL);
      }
      check(true, 'workers close with active deadline fetches');
    });
  }
  await sleep(5);
  self.removeEventListener('unhandledrejection', unhandled);
  result.done = true;
  return result;
}
