/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

// A test to ensure reloading a page doesn't break the inspector.

// Reload should reselect the currently selected markup view element.
// This should work even when an element whose selector is inaccessible
// is selected (bug 1038651).
const TEST_URI = 'data:text/xml,<?xml version="1.0" standalone="no"?>' +
'<!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.1//EN"' +
'  "http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd">' +
'<svg width="4cm" height="4cm" viewBox="0 0 400 400"' +
'     xmlns="http://www.w3.org/2000/svg" version="1.1">' +
"  <title>Example triangle01- simple example of a path</title>" +
"  <desc>A path that draws a triangle</desc>" +
'  <rect x="1" y="1" width="398" height="398"' +
'        fill="none" stroke="blue" />' +
'  <path d="M 100 100 L 300 100 L 200 300 z"' +
'        fill="red" stroke="blue" stroke-width="3" />' +
"</svg>";

add_task(function* () {
  let { inspector, toolbox } = yield openInspectorForURL(TEST_URI);
  let target = toolbox.target;
  let navigationCount = 0;
  let newRootCount = 0;
  let onNavigate = () => navigationCount++;
  let onNewRoot = () => newRootCount++;
  target.on("navigate", onNavigate);
  inspector.on("new-root", onNewRoot);
  registerCleanupFunction(() => {
    target.off("navigate", onNavigate);
    inspector.off("new-root", onNewRoot);
  });

  yield reloadAndCheck(1);

  let svgFront = yield getNodeFront("svg", inspector);
  is(inspector.selection.nodeFront, svgFront, "<svg> selected after reload.");

  info("Selecting a node to see that inspector still works.");
  yield selectNode("rect", inspector);

  yield reloadAndCheck(2);

  let rectFront = yield getNodeFront("rect", inspector);
  is(inspector.selection.nodeFront, rectFront, "<rect> selected after reload.");

  function* reloadAndCheck(expectedCount) {
    let previousRoot = inspector.walker.rootNode.actorID;
    let onNavigated = target.once("navigate");

    info("Reloading page " + expectedCount + ".");
    yield navigateTo(inspector, TEST_URI);
    yield onNavigated;

    is(navigationCount, expectedCount, "The navigation completed.");
    is(newRootCount, expectedCount, "The inspector received a new root.");
    isnot(inspector.walker.rootNode.actorID, previousRoot,
          "The reloaded document has a new root actor.");
  }
});
