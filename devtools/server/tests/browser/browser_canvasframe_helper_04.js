/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test the CanvasFrameAnonymousContentHelper re-inserts the content when the
// page reloads.

// This makes sure the 'domnode' protocol actor type is known when importing
// highlighter.
require("devtools/server/actors/inspector");
const events = require("sdk/event/core");

const {HighlighterEnvironment} = require("devtools/server/actors/highlighters");

const {
  CanvasFrameAnonymousContentHelper
} = require("devtools/server/actors/highlighters/utils/markup");

// browserLoaded matches documentURI, where spaces are percent-encoded.
const TEST_URL_1 = "data:text/html;charset=utf-8," +
  encodeURIComponent("CanvasFrameAnonymousContentHelper test 1");
const TEST_URL_2 = "data:text/html;charset=utf-8," +
  encodeURIComponent("CanvasFrameAnonymousContentHelper test 2");

add_task(function* () {
  let browser = yield addTab(TEST_URL_1);
  let doc = browser.contentDocument;
  let env = new HighlighterEnvironment();
  env.initFromWindow(doc.defaultView);

  let nodeBuilder = () => {
    // The helper rebuilds during the navigation progress notification, before
    // this task resumes after browserLoaded. Never use the previous document.
    let currentDoc = env.document;
    let root = currentDoc.createElement("div");
    let child = currentDoc.createElement("div");
    child.style = "pointer-events:auto;width:200px;height:200px;background:red;";
    child.id = "child-element";
    child.className = "child-element";
    child.textContent = "test content";
    root.appendChild(child);
    return root;
  };

  info("Building the helper");
  let helper = new CanvasFrameAnonymousContentHelper(env, nodeBuilder);

  info("Get an element from the helper");
  let el = helper.getElement("child-element");

  info("Try to access the element");
  is(el.getAttribute("class"), "child-element",
    "The attribute is correct before navigation");
  is(el.getTextContent(), "test content",
    "The text content is correct before navigation");

  info("Add an event listener on the element");
  let mouseDownHandled = 0;
  function onMouseDown(e, id) {
    is(id, "child-element", "The mousedown event was triggered on the element");
    mouseDownHandled++;
  }
  el.addEventListener("mousedown", onMouseDown);

  info("Synthesizing an event on the element");
  let onDocMouseDown = once(doc, "mousedown");
  synthesizeMouseDown(100, 100, doc.defaultView);
  yield onDocMouseDown;
  is(mouseDownHandled, 1, "The mousedown event was handled once before navigation");

  info("Navigating to a new page");
  let previousDoc = doc;
  let loaded = BrowserTestUtils.browserLoaded(browser, false, TEST_URL_2);
  let navigated = once(env, "navigate");
  content.location = TEST_URL_2;
  yield Promise.all([loaded, navigated]);
  doc = browser.contentDocument;
  is(doc.documentURI, TEST_URL_2, "Navigation loaded the expected document");
  isnot(doc, previousDoc, "Navigation replaced the document");
  is(helper.anonymousContentDocument, doc,
    "The helper reinserted its content into the new document");

  info("Try to access the element again");
  is(el.getAttribute("class"), "child-element",
    "The attribute is correct after navigation");
  is(el.getTextContent(), "test content",
    "The text content is correct after navigation");

  info("Synthesizing an event on the element again");
  onDocMouseDown = once(doc, "mousedown");
  synthesizeMouseDown(100, 100, doc.defaultView);
  yield onDocMouseDown;
  is(mouseDownHandled, 1, "The mousedown event was not handled after navigation");

  info("Destroying the helper");
  env.destroy();
  helper.destroy();

  gBrowser.removeCurrentTab();
});

function synthesizeMouseDown(x, y, win) {
  // We need to make sure the inserted anonymous content can be targeted by the
  // event right after having been inserted, and so we need to force a sync
  // reflow.
  let forceReflow = win.document.documentElement.offsetWidth;
  EventUtils.synthesizeMouseAtPoint(x, y, {type: "mousedown"}, win);
}
