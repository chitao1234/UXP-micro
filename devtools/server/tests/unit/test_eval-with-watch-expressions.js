/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Public and watch results must belong to the same final pause, without
// extending either result to thread lifetime or evaluating public code twice.
add_task(function* () {
  do_print("eval-with-watches: setup");
  initTestDebuggerServer();
  let debuggee = addTestGlobal("eval-with-watches");
  let client = new DebuggerClient(DebuggerServer.connectPipe());
  yield client.connect();
  do_check_true(client.mainRoot.traits.clientEvaluateWithWatchExpressions);
  let [,, thread] = yield attachTestTabAndResume(client, "eval-with-watches");
  let closed = false;
  let cleanup = Task.async(function* () {
    if (closed) {
      return;
    }
    do_print("eval-with-watches: resume and close");
    try {
      if (thread.paused) {
        yield thread.resume();
      }
    } finally {
      try {
        yield client.close();
      } finally {
        closed = true;
        DebuggerServer.destroy();
      }
    }
  });
  do_register_cleanup(cleanup);

  try {
    let paused = yield executeOnNextTickAndWaitForPause(() => debuggee.eval(`
      var publicCalls = 0;
      var watchCalls = 0;
      var getterCalls = 0;
      var shared = { value: 1 };
      var getterObject = { get value() { getterCalls++; return shared; } };
      function older() {
        var marker = "older";
        younger();
      }
      function younger() {
        var marker = "younger";
        debugger;
      }
      older();
    `), client);

    function evaluate(expression, watches, frame = null) {
      let done = new Promise((resolve, reject) => {
        let onPaused = (event, packet) => {
          client.removeListener("paused", onPaused);
          resolve(packet);
        };
        client.addListener("paused", onPaused);
        thread.evalWithWatchExpressions(frame, expression, watches).then(null, error => {
          client.removeListener("paused", onPaused);
          reject(new Error(error.message || error.error));
        });
      });
      return done.then(packet => {
        do_check_eq(packet.why.type, "clientEvaluated");
        return packet.why;
      });
    }

    function properties(grip) {
      return new Promise(resolve => {
        thread.pauseGrip(grip).getPrototypeAndProperties(resolve);
      }).then(response => {
        do_check_eq(response.error, undefined);
        return response.ownProperties;
      });
    }

    do_print("eval-with-watches: public object, mutation, and getter");
    let why = yield evaluate(
      "(++publicCalls, shared.value++, getterObject.value)",
      "(++watchCalls, [shared, shared.value, getterCalls])");
    let publicActor = why.frameFinished.return.actor;
    let watchActor = why.watchExpressionsFinished.return.actor;
    let publicProperties = yield properties(why.frameFinished.return);
    let watchProperties = yield properties(why.watchExpressionsFinished.return);
    do_check_eq(publicProperties.value.value, 2);
    do_check_eq(watchProperties[0].value.actor, publicActor);
    do_check_eq(watchProperties[1].value, 2);
    do_check_eq(watchProperties[2].value, 1);
    do_check_eq(debuggee.publicCalls, 1);
    do_check_eq(debuggee.watchCalls, 1);
    do_check_eq(debuggee.getterCalls, 1);

    // Public evaluation uses the selected frame; watches still use depth zero.
    do_print("eval-with-watches: selected frame and expired actors");
    let frames = yield thread.getFrames(0, 2);
    why = yield evaluate("marker", "[marker]", frames.frames[1].actor);
    do_check_eq(why.frameFinished.return, "older");
    watchProperties = yield properties(why.watchExpressionsFinished.return);
    do_check_eq(watchProperties[0].value, "younger");
    for (let actor of [publicActor, watchActor]) {
      let response = yield client.request({ to: actor, type: "bogusRequest" })
        .then(() => { throw new Error("Expired actor unexpectedly answered."); }, error => error);
      do_check_eq(response.error, "noSuchActor");
    }

    // A public throw, including a falsy value, remains a throw completion.
    do_print("eval-with-watches: public throw");
    why = yield evaluate("throw 0", "[shared]");
    do_check_eq(why.frameFinished.throw, 0);
    watchProperties = yield properties(why.watchExpressionsFinished.return);
    yield properties(watchProperties[0].value);

    // An unsanitized watch throw is reported separately; the public object is
    // still live in this pause, so the controller can reject it before retrying.
    do_print("eval-with-watches: watch throw and syntax error");
    why = yield evaluate("shared", "throw ''");
    do_check_eq(why.watchExpressionsFinished.throw, "");
    yield properties(why.frameFinished.return);
    why = yield evaluate("({publicResult: true})", "({");
    do_check_eq(why.watchExpressionsFinished.throw.class, "Error");
    yield properties(why.watchExpressionsFinished.throw);
    yield properties(why.frameFinished.return);

    // Force actual Debugger API termination of one eval using its debugger hook.
    // Return null from onDebuggerStatement, rather than simulating a protocol
    // completion, and restore the normal hook before the following watch eval.
    do_print("eval-with-watches: public termination");
    let connID = Object.keys(DebuggerServer._connections)[0];
    let actor = DebuggerServer._connections[connID].getActor(thread.actor);
    let onDebuggerStatement = actor.dbg.onDebuggerStatement;
    actor.dbg.onDebuggerStatement = () => {
      actor.dbg.onDebuggerStatement = onDebuggerStatement;
      return null;
    };
    why = yield evaluate("debugger; shared", "[shared]");
    do_check_true(why.frameFinished.terminated);
    watchProperties = yield properties(why.watchExpressionsFinished.return);
    yield properties(watchProperties[0].value);

    do_print("eval-with-watches: watch termination");
    actor.dbg.onDebuggerStatement = () => {
      actor.dbg.onDebuggerStatement = onDebuggerStatement;
      return null;
    };
    why = yield evaluate("shared", "debugger; [shared]");
    do_check_true(why.watchExpressionsFinished.terminated);
    yield properties(why.frameFinished.return);

    // Malformed optional input must fail before resuming or invalidating grips.
    do_print("eval-with-watches: malformed watch input");
    let response = yield thread.evalWithWatchExpressions(null, "shared", null)
      .then(() => { throw new Error("Malformed watches unexpectedly accepted."); }, error => error);
    do_check_eq(response.error, "badParameterType");
    do_check_eq(thread.state, "paused");
    yield properties(why.frameFinished.return);

    // The original zero-watch API still has exactly its original packet shape.
    do_print("eval-with-watches: original zero-watch API");
    let done = waitForPause(client);
    thread.eval(null, "shared");
    paused = yield done;
    do_check_false("watchExpressionsFinished" in paused.why);
    yield properties(paused.why.frameFinished.return);
  } finally {
    // An assertion can fail while the debuggee's nested event loop is active.
    // Release that pause before the task rejects, or harness cleanup cannot run.
    yield cleanup();
  }
});
