/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Coverage ranges belong to generated CSS, not to its Sass original sources.
const CSSCoverage = require("devtools/shared/fronts/csscoverage");
const SOURCE_MAP_PREF = "devtools.styleeditor.source-maps-enabled";
const UNUSED_CLASS = "cm-unused-line";

add_task(function* () {
  yield SpecialPowers.pushPrefEnv({set: [[SOURCE_MAP_PREF, true]]});
  let {toolbox, ui} = yield openStyleEditorForURL(TEST_BASE_HTTPS + "sourcemaps.html");
  let usage = yield CSSCoverage.getUsage(toolbox.target);
  let restorers = [];
  let recording = false;
  registerCleanupFunction(function* () {
    while (restorers.length) {
      restorers.pop()();
    }
    if (recording) {
      yield usage.stop();
    }
  });

  let original = ui.editors.find(editor =>
    editor.styleSheet.isOriginalSource && editor.styleSheet.href.endsWith("contained.scss"));
  let otherOriginal = ui.editors.find(editor =>
    editor.styleSheet.isOriginalSource && editor.styleSheet.href.endsWith("sourcemaps.scss"));
  ok(original && otherOriginal, "The fixture exposes two distinct Sass originals");

  let originalSourceEditor = yield selectCoverageEditor(ui, original);
  original.addUnusedRegion({start: {line: 1, column: 0}});
  ok(originalSourceEditor.hasLineClass(0, UNUSED_CLASS),
     "An existing unused marker is present before reselecting the original");
  yield selectCoverageEditor(ui, otherOriginal);
  // Finish the preceding selection's capability lookup before observing this one.
  yield CSSCoverage.getUsage(toolbox.target);

  let totalRequests = 0;
  let originalRequests = 0;
  let generatedRequests = 0;
  let originalFinished = defer();
  observeCoverageMethod(usage, "createEditorReportForSheet", sheet => {
    totalRequests++;
    if (sheet.isOriginalSource) {
      originalRequests++;
      if (sheet.actorID == original.styleSheet.actorID) {
        // Let the unfixed control fail promptly; the real RPC still runs and
        // its rejection is not caught or suppressed by this observer.
        originalFinished.resolve("invalid-original-request");
      }
    } else {
      generatedRequests++;
    }
  }, restorers);
  observeCoverageMethod(original, "removeAllUnusedRegions", () => {
    originalFinished.resolve("markers-cleared");
  }, restorers);

  let requestsBefore = totalRequests;
  yield selectCoverageEditor(ui, original);
  is(yield originalFinished.promise, "markers-cleared",
     "Original-source coverage processing finishes by clearing stale markers");
  is(totalRequests, requestsBefore,
     "Selecting an original source makes no generated-CSS coverage request");
  is(originalRequests, 0, "No OriginalSourceFront is passed to the stylesheet RPC");
  ok(!originalSourceEditor.hasLineClass(0, UNUSED_CLASS),
     "Stale generated-CSS markers are removed from the Sass editor");
  ok(originalSourceEditor.getText().includes("$pink"),
     "The original editor still contains the Sass source");

  let reset = ui.once("stylesheets-reset");
  let selected = ui.once("editor-selected");
  yield SpecialPowers.pushPrefEnv({set: [[SOURCE_MAP_PREF, false]]});
  yield Promise.all([reset, selected]);
  let generated = ui.editors.find(editor =>
    !editor.styleSheet.isOriginalSource && editor.styleSheet.href.endsWith("contained.css"));
  let alternate = ui.editors.find(editor => editor != generated);
  ok(generated && alternate, "Generated CSS editors remain available");

  ok(!usage.isRunning(), "Coverage starts in the stopped state");
  yield usage.start(window, toolbox.target, true);
  recording = true;
  ok(usage.isRunning(), "Real CSS coverage collection starts");
  yield usage.stop();
  recording = false;
  ok(!usage.isRunning(), "Coverage collection stops before displaying its report");

  let {reports} = yield usage.createEditorReportForSheet(generated.styleSheet);
  is(reports.length, 1, "The stopped collection retains its unused generated rule");
  is(reports[0].selectorText, "#header", "The retained report identifies the unused selector");
  is(reports[0].start.line, 1, "The retained range uses the generated CSS line");

  yield selectCoverageEditor(ui, alternate);
  yield CSSCoverage.getUsage(toolbox.target);
  let marked = defer();
  observeCoverageMethod(generated, "addUnusedRegions", regions => {
    marked.resolve(regions);
  }, restorers);
  let requestsBeforeMarking = generatedRequests;
  let generatedSourceEditor = yield selectCoverageEditor(ui, generated);
  let applied = yield marked.promise;
  is(applied.length, 1, "Actual editor selection applies the retained coverage range");
  is(applied[0].selectorText, "#header", "The UI applies the real generated-sheet report");
  ok(generatedRequests > requestsBeforeMarking,
     "The real selection path reaches the observed generated-sheet RPC");
  ok(generatedSourceEditor.hasLineClass(0, UNUSED_CLASS),
     "The unused generated selector has a real source-editor marker");
  ok(!usage.isRunning(), "Stopped coverage remains usable without restarting collection");
});

function* selectCoverageEditor(ui, editor) {
  if (ui.selectedEditor != editor) {
    let selected = defer();
    function onSelected(event, selectedEditor) {
      if (selectedEditor == editor) {
        ui.off("editor-selected", onSelected);
        selected.resolve();
      }
    }
    ui.on("editor-selected", onSelected);
    registerCleanupFunction(() => ui.off("editor-selected", onSelected));
    yield Promise.all([ui.selectStyleSheet(editor.styleSheet), selected.promise]);
  }
  is(ui.selectedEditor, editor, "The requested editor is actually selected");
  yield editor.getSourceEditor();
  return editor.sourceEditor;
}

function observeCoverageMethod(object, name, afterCall, restorers) {
  let descriptor = Object.getOwnPropertyDescriptor(object, name);
  let original = object[name];
  object[name] = function (...args) {
    let result = original.apply(this, args);
    afterCall.apply(this, args);
    return result;
  };
  restorers.push(() => {
    if (descriptor) {
      Object.defineProperty(object, name, descriptor);
    } else {
      delete object[name];
    }
  });
}
