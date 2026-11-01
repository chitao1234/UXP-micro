/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Test that the keybindings for Picker work alright

const TEST_URL = URL_ROOT + "doc_inspector_highlighter_dom.html";

add_task(function* () {
  let {inspector, toolbox, testActor} = yield openInspectorForURL(TEST_URL);

  yield startPicker(toolbox);

  info("Selecting the simple-div1 DIV");
  yield moveMouseOver("#simple-div1");

  ok((yield testActor.isHighlighting()), "The picker highlighter is visible");
  yield testActor.isNodeCorrectlyHighlighted("#simple-div1", is, "picker hover");

  // First Child selection
  info("Testing first-child selection.");

  yield doKeyHover({key: "VK_RIGHT", options: {}});
  ok((yield testActor.isHighlighting()), "The first-child highlighter is visible");
  yield testActor.isNodeCorrectlyHighlighted("#useless-para", is, "first child");

  info("Selecting the useful-para paragraph DIV");
  yield moveMouseOver("#useful-para");
  ok((yield testActor.isHighlighting()), "The picker highlighter is visible");
  yield testActor.isNodeCorrectlyHighlighted("#useful-para", is, "picker hover");

  yield doKeyHover({key: "VK_RIGHT", options: {}});
  ok((yield testActor.isHighlighting()), "The first-child highlighter is visible");
  yield testActor.isNodeCorrectlyHighlighted("#bold", is, "first child");

  info("Going back up to the simple-div1 DIV");
  yield doKeyHover({key: "VK_LEFT", options: {}});
  yield doKeyHover({key: "VK_LEFT", options: {}});
  ok((yield testActor.isHighlighting()), "The parent highlighter is visible");
  yield testActor.isNodeCorrectlyHighlighted("#simple-div1", is, "parent navigation");

  info("First child selection test Passed.");

  info("Stopping the picker");
  yield toolbox.highlighterUtils.stopPicker();

  function doKeyHover(args) {
    info("Key pressed. Waiting for element to be highlighted/hovered");
    testActor.synthesizeKey(args);
    return inspector.toolbox.once("picker-node-hovered");
  }

  function moveMouseOver(selector) {
    info("Waiting for element " + selector + " to be highlighted");
    testActor.synthesizeMouse({
      options: {type: "mousemove"},
      center: true,
      selector: selector
    });
    return inspector.toolbox.once("picker-node-hovered");
  }
});
