/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const ENTRY_SOURCE = "http://example.com/step-entry-functions.js";
const DRIVER_SOURCE = "http://example.com/step-entry-driver.js";
const BLACKBOX_SOURCE = "http://example.com/step-entry-blackbox.js";

function checkLocation(packet, url, line) {
  logPause("check " + url + ":" + line, packet);
  equal(packet.why.type, "resumeLimit", "The pause is a stepping result");
  equal(packet.frame.where.source.url, url, "The step selects the expected source");
  equal(packet.frame.where.line, line, "The step selects an executable line");
}

function logPause(phase, packet) {
  const where = packet.frame && packet.frame.where;
  do_print("step-entry " + phase + " " + JSON.stringify({
    type: packet.type,
    reason: packet.why,
    where: where && { url: where.source && where.source.url,
                      line: where.line, column: where.column },
    error: packet.error,
  }));
}

function run_test() {
  do_test_pending();
  let client;
  Task.spawn(function* () {
    do_print("step-entry initialize server");
    initTestDebuggerServer();
    const debuggee = addTestGlobal("step-entry-points");
    do_print("step-entry global created");
    client = new DebuggerClient(DebuggerServer.connectPipe());
    let packets = 0;
    client.addListener("paused", (event, packet) => {
      if (packets++ < 30) {
        logPause("pause event " + packets, packet);
      }
    });
    do_print("step-entry connecting client");
    yield client.connect();
    do_print("step-entry client connected; attaching");
    const [,, thread] = yield attachTestTabAndResume(client, "step-entry-points");
    do_print("step-entry attached and resumed");

    Cu.evalInSandbox(
      "function multiline(\n" +       // 1
      "  value\n" +                   // 2
      ") {\n" +                       // 3
      "  this.entryValue = value;\n" + // 4: first executable statement
      "  return value;\n" +           // 5
      "}\n" +                         // 6
      "function sameLine() { this.sameLineHit = true; }\n" + // 7
      "function callback() {\n" +      // 8
      "  this.callbackHit = true;\n" + // 9: first executable statement
      "}\n",                          // 10
      debuggee, "1.8", ENTRY_SOURCE, 1);
    do_print("step-entry entry source evaluated");
    Cu.evalInSandbox(
      "function dispatch(callback) {\n" +
      "  callback();\n" +
      "}\n", debuggee, "1.8", BLACKBOX_SOURCE, 1);
    do_print("step-entry dispatcher source evaluated; requesting sources");

    const { sources } = yield getSources(thread);
    do_print("step-entry sources " + JSON.stringify(sources.map(source => source.url)));
    const blackbox = sources.filter(source => source.url === BLACKBOX_SOURCE)[0];
    ok(!!blackbox, "The blackboxed dispatcher has a real source");
    yield blackBox(thread.source(blackbox));
    do_print("step-entry dispatcher blackboxed; evaluating driver next tick");

    let packet = yield executeOnNextTickAndWaitForPause(() => {
      Cu.evalInSandbox(
        "debugger;\n" +             // 1
        "multiline(42);\n" +        // 2
        "debugger;\n" +             // 3
        "sameLine();\n" +           // 4
        "debugger;\n" +             // 5
        "dispatch(callback);\n" +   // 6
        "debugger;\n",              // 7
        debuggee, "1.8", DRIVER_SOURCE, 1);
    }, client);
    logPause("initial driver pause", packet);
    equal(packet.why.type, "debuggerStatement", "The driver starts paused");

    do_print("step-entry step over initial debugger");
    packet = yield stepOver(client, thread);
    checkLocation(packet, DRIVER_SOURCE, 2);
    do_print("step-entry step into multiline");
    packet = yield stepIn(client, thread);
    checkLocation(packet, ENTRY_SOURCE, 4);
    equal(debuggee.entryValue, undefined, "Entry pauses before executing the first statement");
    do_print("step-entry step over multiline first statement");
    packet = yield stepOver(client, thread);
    checkLocation(packet, ENTRY_SOURCE, 5);
    equal(debuggee.entryValue, 42, "The first statement executes exactly once");

    do_print("step-entry resume to same-line driver pause");
    packet = yield resumeAndWaitForPause(client, thread);
    logPause("same-line driver pause", packet);
    equal(packet.frame.where.line, 3, "Resume reaches the next driver debugger statement");
    do_print("step-entry step over second debugger");
    packet = yield stepOver(client, thread);
    checkLocation(packet, DRIVER_SOURCE, 4);
    do_print("step-entry step into same-line function");
    packet = yield stepIn(client, thread);
    checkLocation(packet, ENTRY_SOURCE, 7);
    equal(debuggee.sameLineHit, undefined, "Same-line function entry is not skipped");

    do_print("step-entry resume to callback driver pause");
    packet = yield resumeAndWaitForPause(client, thread);
    logPause("callback driver pause", packet);
    equal(packet.frame.where.line, 5, "Same-line function returns to the driver");
    equal(debuggee.sameLineHit, true, "Same-line function executes normally");
    do_print("step-entry step over third debugger");
    packet = yield stepOver(client, thread);
    checkLocation(packet, DRIVER_SOURCE, 6);
    do_print("step-entry step through blackboxed dispatcher");
    packet = yield stepIn(client, thread);
    checkLocation(packet, ENTRY_SOURCE, 9);
    equal(debuggee.callbackHit, undefined,
          "Stepping skips the blackboxed dispatcher and callback setup bytecode");

    do_print("step-entry resume to final debugger");
    packet = yield resumeAndWaitForPause(client, thread);
    logPause("final driver pause", packet);
    equal(packet.frame.where.line, 7, "The callback returns to the final driver pause");
    equal(debuggee.callbackHit, true, "The callback executes normally");
    do_print("step-entry resume final debugger");
    yield resume(thread);
    do_print("step-entry final resume complete; closing client");
    yield client.close();
    do_print("step-entry client closed");
    DebuggerServer.destroy();
    do_test_finished();
  }).then(null, error => {
    do_print("step-entry failed " + error + "\n" + (error.stack || ""));
    if (client) {
      client.close().then(() => {
        DebuggerServer.destroy();
        do_throw(error);
      }, closeError => {
        do_print("step-entry close failed " + closeError);
        DebuggerServer.destroy();
        do_throw(error);
      });
    } else {
      DebuggerServer.destroy();
      do_throw(error);
    }
  });
}
