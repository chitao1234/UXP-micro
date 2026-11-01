/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

// Breadcrumb capture listeners belong to the outer event target and must stop
// receiving events after normal Inspector teardown, while its DOM is retained.

const TEST_URI = URL_ROOT + "doc_inspector_breadcrumbs_visibility.html";
const FIRST_NODE = "div#aVeryLongIdToExceedTheBreadcrumbTruncationLimit";
const LAST_NODE = "div#aSeventhOneToExceedTheTruncationLimit";
const EVENT_TYPES = ["click", "mouseover", "mouseout", "focus"];

add_task(function* () {
  let {inspector, toolbox} = yield openInspectorForURL(TEST_URI);
  yield selectNode(LAST_NODE, inspector);

  let breadcrumbs = inspector.breadcrumbs;
  let outer = breadcrumbs.outer;
  let panelWindow = inspector.panelWin;
  let panelDocument = inspector.panelDoc;
  let listenerService = Components.classes["@mozilla.org/eventlistenerservice;1"]
    .getService(Components.interfaces.nsIEventListenerService);
  function ownedListeners() {
    return (listenerService.getListenerInfoFor(outer) || []).filter(listener =>
      listener.listenerObject === breadcrumbs && listener.capturing &&
      EVENT_TYPES.indexOf(listener.type) !== -1);
  }

  is(ownedListeners().map(listener => listener.type).sort().join(","),
     EVENT_TYPES.slice().sort().join(","),
     "The live widget owns all four capture listeners on its outer target");
  isnot(outer, breadcrumbs.container,
        "The registered outer target is distinct from the inner scrollport");

  let originalHandleEvent = breadcrumbs.handleEvent;
  let hadOwnHandler = Object.prototype.hasOwnProperty.call(breadcrumbs, "handleEvent");
  let controlledEvents = new Set();
  let receivedEvents = [];
  breadcrumbs.handleEvent = function (event) {
    if (controlledEvents.has(event)) {
      receivedEvents.push({
        type: event.type, target: event.target, currentTarget: event.currentTarget,
        phase: event.eventPhase
      });
    }
    // Preserve both normal handling and genuine post-destroy exceptions.
    return originalHandleEvent.call(this, event);
  };

  function dispatch(type, target) {
    let event = type == "focus"
      ? new panelWindow.FocusEvent(type, {bubbles: false})
      : new panelWindow.MouseEvent(type, {bubbles: true, button: 0});
    controlledEvents.add(event);
    target.dispatchEvent(event);
  }

  try {
    // A real DOM click must pass through outer capture and select its ancestor.
    let front = yield getNodeFront(FIRST_NODE, inspector);
    let button = outer.querySelector('button[title="' + FIRST_NODE + '"]');
    ok(button, "The live ancestor breadcrumb exists");
    let updated = inspector.once("inspector-updated");
    dispatch("click", button);
    yield updated;
    is(inspector.selection.nodeFront, front,
       "The live breadcrumb click changes the real Inspector selection");
    is(receivedEvents.length, 1, "The live controlled click reaches the widget once");
    is(receivedEvents[0].type, "click", "The observed live event is the click");
    is(receivedEvents[0].target, button, "The live event targets the real breadcrumb");
    is(receivedEvents[0].currentTarget, outer,
       "The live event is handled on the registered outer target");
    is(receivedEvents[0].phase, panelWindow.Event.CAPTURING_PHASE,
       "The live click traverses the outer capture listener");

    // Exercise the public Inspector lifecycle before Toolbox releases the panel
    // document. Retaining its outer node models late events during host teardown
    // without relying on a dead-window wrapper or a mouse-pointer location.
    yield inspector.destroy();
    ok(breadcrumbs.isDestroyed, "Normal Inspector teardown destroys its breadcrumbs");
    is(inspector.toolbox, null, "Normal Inspector teardown releases its Toolbox");
    for (let type of EVENT_TYPES) {
      is(ownedListeners().filter(listener => listener.type == type).length, 0,
         "Inspector teardown removes its outer " + type + " capture listener");
    }

    outer.remove();
    ok(!panelDocument.documentElement.contains(outer),
       "The retained outer target is detached from the live panel document");
    for (let type of EVENT_TYPES) {
      let before = receivedEvents.length;
      dispatch(type, outer);
      is(receivedEvents.length, before,
         "The destroyed widget does not handle a retained-target " + type + " event");
    }
  } finally {
    // Remove only this widget's exact registrations, including on a failing
    // old-product control, after its leaked events/errors have been observed.
    for (let type of EVENT_TYPES) {
      outer.removeEventListener(type, breadcrumbs, true);
    }
    if (hadOwnHandler) {
      breadcrumbs.handleEvent = originalHandleEvent;
    } else {
      delete breadcrumbs.handleEvent;
    }
    controlledEvents.clear();
    yield toolbox.destroy();
  }
});
