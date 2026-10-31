/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Test keyboard shortcuts.
 */

function pressResume(dbg) {
  pressKey(dbg, "resumeKey");
  return waitForPaused(dbg);
}

function pressStepOver(dbg) {
  pressKey(dbg, "stepOverKey");
  return waitForPaused(dbg);
}

function pressStepIn(dbg) {
  pressKey(dbg, "stepInKey");
  return waitForPaused(dbg);
}

function pressStepOut(dbg) {
  pressKey(dbg, "stepOutKey");
  return waitForPaused(dbg);
}

add_task(function*() {
  const dbg = yield initDebugger("doc-debugger-statements.html");

  yield reload(dbg);
  yield waitForPaused(dbg);
  assertPausedLocation(dbg, "debugger-statements.html", 8);

  yield pressResume(dbg);
  assertPausedLocation(dbg, "debugger-statements.html", 12);

  yield pressStepIn(dbg);
  assertPausedLocation(dbg, "debugger-statements.html", 13);

  yield pressStepOut(dbg);
  assertPausedLocation(dbg, "debugger-statements.html", 14);

  yield pressStepOver(dbg);
  // The caller's call-cleanup bytecode is not a source entry point. Stop at
  // its next executable statement, as in the newer Firefox keyboard fixture.
  assertPausedLocation(dbg, "debugger-statements.html", 10);
  is(dbg.selectors.getSelectedFrame(dbg.getState()).displayName, "(global)",
     "Stepping out reaches the caller's global frame");
  yield ContentTask.spawn(gBrowser.selectedBrowser, null, function* () {
    Assert.ok(!content.wrappedJSObject.shortcutTestDone,
              "The caller's next statement has not run at the step stop");
  });

  const resumed = waitForThreadEvents(dbg, "resumed");
  pressKey(dbg, "resumeKey");
  yield resumed;
  yield ContentTask.spawn(gBrowser.selectedBrowser, null, function* () {
    Assert.equal(content.wrappedJSObject.shortcutTestDone, true,
                 "The caller's next statement runs after resuming");
  });
});
