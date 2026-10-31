/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests basic pretty-printing functionality.

add_task(function* () {
  const dbg = yield initDebugger("doc-minified.html");
  const { selectors: { getSelectedSource }, getState } = dbg;
  const generatedSource = findSource(dbg, "math.min.js");

  yield selectSource(dbg, generatedSource);
  ok(findElement(dbg, "prettyPrintButton"), "Pretty-print button is shown for the generated source");
  clickElement(dbg, "prettyPrintButton");
  yield waitForDispatch(dbg, "TOGGLE_PRETTY_PRINT");

  const ppSrc = findSource(dbg, "math.min.js:formatted");
  ok(ppSrc, "Pretty-printed source exists");

  yield addBreakpoint(dbg, ppSrc, 18);

  invokeInTab("arithmetic");
  yield waitForPaused(dbg);
  assertPausedLocation(dbg, ppSrc, 18);
  yield stepOver(dbg);
  assertPausedLocation(dbg, ppSrc, 27);
  yield resume(dbg);

  // The pretty-print button should go away in the pretty-printed
  // source.
  ok(!findElement(dbg, "prettyPrintButton"), "Pretty-print button is hidden for the formatted source");

  // A substring lookup also matches math.min.js:formatted. Select the exact
  // generated source captured before pretty-printing instead.
  yield selectSource(dbg, generatedSource);
  is(getSelectedSource(getState()).get("id"), generatedSource.id,
     "The generated source is selected again");
  ok(findElement(dbg, "prettyPrintButton"), "Pretty-print button is shown for the generated source");
});
