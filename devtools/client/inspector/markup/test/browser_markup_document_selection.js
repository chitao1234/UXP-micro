/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// The walker's root document owns the tree but has no rendered editor row.
// Selecting it must clear the markup selection without replacing the global
// document selection, and later real-node selections must still complete.
const TEST_URI = "data:text/html;charset=utf-8," + encodeURIComponent(`
  <div id="before">Before</div>
  <section><div><span id="nested">Nested</span></div></section>
`);

add_task(function* () {
  let {inspector} = yield openInspectorForURL(TEST_URI);
  let markup = inspector.markup;
  let documentRoot = inspector.walker.rootNode;

  yield selectNode("#before", inspector);
  let before = inspector.selection.nodeFront;
  let beforeContainer = markup.getContainer(before);
  is(markup._selectedContainer, beforeContainer,
     "A real node has a selected markup row");
  ok(beforeContainer.selected, "The first real row is selected");
  let rootContainer = markup.getContainer(documentRoot);
  rootContainer.elt.focus();
  EventUtils.synthesizeKey("VK_RETURN", {}, markup.win);
  is(markup.doc.activeElement, beforeContainer.editor.tag,
     "A genuine Enter key focuses the selected real row's editor");
  ok(beforeContainer.canFocus, "The real row has enabled editable tab stops");

  let onUpdated = inspector.once("inspector-updated");
  inspector.selection.setNodeFront(documentRoot, "test");
  is(inspector.selection.nodeFront, documentRoot,
     "The inspector preserves the requested global document selection");
  is(markup._selectedContainer, null,
     "Selecting the dummy document synchronously clears the rendered selection");
  ok(!rootContainer.elt.hasAttribute("aria-activedescendant"),
     "The non-rendered document has no stale active tree row");
  ok(!beforeContainer.selected, "The previous real row is unselected");
  ok(!beforeContainer.canFocus,
     "Discarding the rendered selection disables its editable tab stops");
  ok(beforeContainer.focusableElms.every(element =>
    element.getAttribute("tabindex") === "-1"),
     "Every former editor item is removed from sequential tab navigation");
  ok(!beforeContainer.elt.contains(markup.doc.activeElement),
     "The discarded real row no longer retains keyboard focus");
  yield onUpdated;
  is(markup._selectedContainer, null,
     "The document update completes without selecting the non-rendered wrapper");

  rootContainer.elt.focus();
  is(markup.doc.activeElement, rootContainer.elt,
     "Real keyboard input targets the markup tree");
  let errors = observeMarkupErrors();
  try {
    ok(!markup.isDragging, "Escape is tested outside a drag operation");
    for (let key of ["VK_LEFT", "VK_RIGHT", "VK_UP", "VK_DOWN",
                     "VK_PAGE_UP", "VK_PAGE_DOWN", "VK_RETURN", "VK_SPACE",
                     "VK_BACK_SPACE", "VK_DELETE", "VK_ESCAPE"]) {
      let received = [];
      let onKeyDown = event => received.push(event.defaultPrevented);
      markup.win.addEventListener("keydown", onKeyDown);
      try {
        EventUtils.synthesizeKey(key, {}, markup.win);
      } finally {
        markup.win.removeEventListener("keydown", onKeyDown);
      }
      is(received.length, 1, key + " genuinely reaches the tree window");
      is(received[0], false,
         key + " is not consumed without a selected rendered row");
      is(inspector.selection.nodeFront, documentRoot,
         key + " preserves the global document selection");
      is(markup._selectedContainer, null,
         key + " does not invent a selected markup row");
    }

    // Drain posted console notifications, rather than wait for an arbitrary
    // timeout. The observer only records errors; normal reporting stays active.
    yield new Promise(resolve => executeSoon(resolve));
    is(errors.messages.length, 0,
       "Real row-dependent shortcuts report no markup exceptions");

    let firstContainer = rootContainer.getChildContainers()[0];
    ok(firstContainer, "A real first row exists for absolute Home navigation");
    rootContainer.elt.focus();
    onUpdated = inspector.once("inspector-updated");
    EventUtils.synthesizeKey("VK_HOME", {}, markup.win);
    yield onUpdated;
    is(markup._selectedContainer, firstContainer,
       "Home recovers the first real row from a non-rendered document selection");
    is(inspector.selection.nodeFront, firstContainer.node,
       "Valid absolute navigation updates the global selection");
  } finally {
    errors.stop();
  }

  yield selectNode("#nested", inspector);
  let nested = inspector.selection.nodeFront;
  let nestedContainer = markup.getContainer(nested);
  is(markup._selectedContainer, nestedContainer,
     "A nested real-node selection completes after the dummy document");
  ok(nestedContainer.selected, "The nested real row is selected");
  ok(nestedContainer.editor.elt.isConnected,
     "The nested real row has an attached editor");

  yield selectNode("html", inspector);
  let documentElement = inspector.selection.nodeFront;
  let documentElementContainer = markup.getContainer(documentElement);
  ok(documentElement !== documentRoot,
     "The document element is distinct from the walker's dummy root");
  is(markup._selectedContainer, documentElementContainer,
     "The actual documentElement remains a selectable rendered row");
  ok(documentElementContainer.selected,
     "The actual documentElement row is selected");
});

add_task(function* () {
  let {inspector} = yield openInspectorForURL(TEST_URI);
  let markup = inspector.markup;
  let documentRoot = inspector.walker.rootNode;
  yield selectNode(documentRoot, inspector);

  info("Remove the real document element and wait for the actual markup mutation");
  let onMutation = inspector.once("markupmutation");
  yield ContentTask.spawn(gBrowser.selectedBrowser, null, function* () {
    content.document.documentElement.remove();
  });
  yield onMutation;
  yield markup._waitForChildren();

  let rootContainer = markup.getContainer(documentRoot);
  is(rootContainer.getChildContainers().length, 0,
     "The real document has no remaining markup rows");
  rootContainer.elt.focus();
  let errors = observeMarkupErrors();
  try {
    EventUtils.synthesizeKey("VK_HOME", {}, markup.win);
    yield new Promise(resolve => executeSoon(resolve));
    is(errors.messages.length, 0,
       "Home on a genuinely empty document reports no exception");
    is(markup._selectedContainer, null,
       "Home cannot select a row that does not exist");
    is(inspector.selection.nodeFront, documentRoot,
       "Home preserves global document selection when no real row exists");
  } finally {
    errors.stop();
  }
});

add_task(function* () {
  let {inspector} = yield openInspectorForURL(TEST_URI);
  let markup = inspector.markup;
  let documentRoot = inspector.walker.rootNode;
  yield selectNode("#before", inspector);
  let nested = yield getNodeFront("#nested", inspector);

  let releaseReveal;
  let revealGate = new Promise(resolve => {
    releaseReveal = resolve;
  });
  let revealHeld;
  let onRevealHeld = new Promise(resolve => {
    revealHeld = resolve;
  });
  let pendingReveal;
  let originalShowNode = markup.showNode;
  let originalMarkNodeAsSelected = markup.markNodeAsSelected;
  let markedNodes = [];
  markup.showNode = function (node) {
    let actualReveal = originalShowNode.apply(this, arguments);
    if (node !== nested) {
      return actualReveal;
    }
    // Complete the genuine reveal, then hold only its completion notification.
    // This deterministically exercises the real pending-selection callback.
    pendingReveal = actualReveal.then(() => {
      revealHeld();
      return revealGate;
    });
    return pendingReveal;
  };
  markup.markNodeAsSelected = function (node) {
    markedNodes.push(node);
    return originalMarkNodeAsSelected.apply(this, arguments);
  };

  try {
    inspector.selection.setNodeFront(nested, "test");
    yield onRevealHeld;
    let onUpdated = inspector.once("inspector-updated");
    inspector.selection.setNodeFront(documentRoot, "test");
    yield onUpdated;
    is(markup._selectedContainer, null,
       "A new document selection clears the row while an old reveal is held");

    releaseReveal();
    yield pendingReveal;
    yield new Promise(resolve => executeSoon(resolve));
    is(inspector.selection.nodeFront, documentRoot,
       "Completing an obsolete reveal preserves the newer global document selection");
    is(markup._selectedContainer, null,
       "Completing an obsolete reveal cannot select the dummy root as a row");
    ok(!markedNodes.includes(documentRoot),
       "The obsolete real-node callback never marks the non-rendered document");
    ok(!markup.getContainer(documentRoot).elt.hasAttribute("aria-activedescendant"),
       "The obsolete reveal cannot restore a stale active tree row");
  } finally {
    releaseReveal();
    markup.showNode = originalShowNode;
    markup.markNodeAsSelected = originalMarkNodeAsSelected;
  }

  yield selectNode(nested, inspector);
  is(markup._selectedContainer, markup.getContainer(nested),
     "A later valid nested-row selection still completes normally");
});

add_task(function* () {
  let {inspector} = yield openInspectorForURL(TEST_URI);
  let documentRoot = inspector.walker.rootNode;
  yield selectNode(documentRoot, inspector);

  info("Rebuild the genuine markup frame while the document remains selected");
  yield inspector._destroyMarkup();
  let onMarkupLoaded = inspector.once("markuploaded");
  inspector._initMarkup();
  yield onMarkupLoaded;

  let markup = inspector.markup;
  let rootContainer = markup.getContainer(documentRoot);
  ok(rootContainer, "The rebuilt view imports its real document tree wrapper");
  is(markup._rootNode, documentRoot, "The rebuilt view owns the correct tree root");
  is(markup._selectedContainer, null,
     "Initializing the tree does not turn its dummy root into a rendered row");
  is(inspector.selection.nodeFront, documentRoot,
     "Rebuilding preserves the global document selection");
  markup.expandNode(documentRoot);
  yield markup._waitForChildren();
  ok(rootContainer.elt.isConnected, "The rebuilt tree is attached to the live frame");
  ok(rootContainer.getChildContainers().length > 0,
     "The rebuilt tree loads and renders its genuine document children");

  yield selectNode("#nested", inspector);
  is(markup._selectedContainer, markup.getContainer(inspector.selection.nodeFront),
     "A real nested row can be selected after root-selected initialization");
});

function observeMarkupErrors() {
  let messages = [];
  let observer = subject => {
    let message = subject.wrappedJSObject;
    if (message.level !== "error") {
      return;
    }
    let text = message.arguments.map(String).join(" ");
    if (text.includes("inspector/markup/markup.js")) {
      messages.push(text);
      info("Observed actual markup error: " + text);
    }
  };
  Services.obs.addObserver(observer, "console-api-log-event", false);
  return {
    messages,
    stop: () => Services.obs.removeObserver(observer, "console-api-log-event")
  };
}
