/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

// Test that the start and end buttons reveal each ancestor in the breadcrumb
// scrollport, for both LTR and RTL and independently of native font metrics.

let { Toolbox } = require("devtools/client/framework/toolbox");

const TEST_URI = URL_ROOT + "doc_inspector_breadcrumbs_visibility.html";
const NODE_ONE = "div#aVeryLongIdToExceedTheBreadcrumbTruncationLimit";
const NODE_TWO = "div#anotherVeryLongIdToExceedTheBreadcrumbTruncationLimit";
const NODE_THREE = "div#aThirdVeryLongIdToExceedTheTruncationLimit";
const NODE_FOUR = "div#aFourthOneToExceedTheTruncationLimit";
const NODE_FIVE = "div#aFifthOneToExceedTheTruncationLimit";
const NODE_SIX = "div#aSixthOneToExceedTheTruncationLimit";
const NODE_SEVEN = "div#aSeventhOneToExceedTheTruncationLimit";
const CRUMB_TITLES = ["html", "body", NODE_ONE, NODE_TWO, NODE_THREE,
                      NODE_FOUR, NODE_FIVE, NODE_SIX, NODE_SEVEN];

add_task(function* () {
  yield pushPref("devtools.toolsidebar-width.inspector", 350);
  yield pushPref("devtools.toolsidebar-height.inspector", 150);

  let { inspector, toolbox } = yield openInspectorForURL(TEST_URI);

  // Instant scrolling makes each click observable through its real scroll event.
  inspector.breadcrumbs.arrowScrollBox.scrollBehavior = "instant";

  yield toolbox.switchHost(Toolbox.HostType.WINDOW);
  let hostWindow = toolbox.win.parent;
  let originalWidth = hostWindow.outerWidth;
  let originalHeight = hostWindow.outerHeight;
  hostWindow.resizeTo(640, 300);

  let root = inspector.panelDoc.documentElement;
  let originalDirection = root.getAttribute("dir");
  try {
    for (let direction of ["ltr", "rtl"]) {
      // Toolbox sets this public HTML input once when opening the panel. A
      // later XUL locale-pref change does not update that explicit attribute.
      // ArrowScrollBox reads computed direction on each action, so set it
      // before selection/layout. This tests component direction, not live
      // locale switching or the locale-dependent arrow artwork.
      root.setAttribute("dir", direction);
      yield testBreadcrumbTransitions(inspector, direction);
    }
  } finally {
    root.setAttribute("dir", originalDirection);
    hostWindow.resizeTo(originalWidth, originalHeight);
  }
});

function* testBreadcrumbTransitions(inspector, direction) {
  let breadcrumbs = inspector.panelDoc.getElementById("inspector-breadcrumbs");
  let container = breadcrumbs.querySelector(".html-arrowscrollbox-inner");
  is(inspector.panelWin.getComputedStyle(container).direction, direction,
     "The breadcrumb component actually uses " + direction);

  // Select a different node first, so both direction passes get a real update.
  yield selectNode(NODE_ONE, inspector);
  let updated = inspector.once("breadcrumbs-updated");
  yield selectNode(NODE_SEVEN, inspector);
  yield updated;

  let buttons = Array.from(container.querySelectorAll("button"));
  is(buttons.map(button => button.getAttribute("title")).join("\n"),
     CRUMB_TITLES.join("\n"), "All nine ancestor crumbs are present");
  ok(container.scrollWidth > container.clientWidth,
     "The native-size breadcrumb fixture really overflows");
  let port = getScrollport(container);
  ok(buttons.every(button => button.getBoundingClientRect().width <= port.width),
     "Each individual crumb can fit fully inside the scrollport");
  ok(isElementInScrollport(container, container.querySelector("button[checked]")),
     "The selected final crumb is fully visible initially");
  ok(!isElementInScrollport(container, buttons[0]),
     "The visibility oracle rejects the initially clipped first crumb");

  for (let action of ["start", "end"]) {
    yield walkBreadcrumbs(inspector, container, buttons, direction, action);
  }
}

function* walkBreadcrumbs(inspector, container, buttons, direction, action) {
  let visited = new Set();
  recordVisibleCrumbs(container, buttons, visited);
  let button = inspector.panelDoc.querySelector(action == "start"
    ? "#inspector-breadcrumbs .scrollbutton-up"
    : "#inspector-breadcrumbs .scrollbutton-down");
  let clicks = 0;

  // A successful click reveals the next outside/partial crumb; at most one
  // click per crumb is needed. Do not assume only one crumb was initially
  // visible, or use the widget's private target finder as the test oracle.
  for (let step = 0; step < buttons.length; step++) {
    let target = nextOutsideCrumb(container, buttons, direction, action);
    if (!target) {
      break;
    }

    let title = target.getAttribute("title");
    info(direction + " " + action + ": revealing " + title);
    let before = buttons[0].getBoundingClientRect().left;
    let onScroll;
    let scrolled = new Promise(resolve => {
      onScroll = resolve;
      container.addEventListener("scroll", onScroll);
    });

    try {
      EventUtils.synthesizeMouseAtCenter(button, {}, inspector.panelWin);
      let after = buttons[0].getBoundingClientRect().left;
      let movingRight = (action == "start") == (direction == "ltr");
      let progressed = movingRight ? after > before : after < before;
      ok(progressed, direction + " " + action + " makes real scroll progress");
      if (!progressed) {
        return;
      }
      yield scrolled;
    } finally {
      container.removeEventListener("scroll", onScroll);
    }

    clicks++;
    ok(isElementInScrollport(container, target),
       direction + " " + action + " fully reveals " + title);
    recordVisibleCrumbs(container, buttons, visited);
  }

  ok(clicks > 0, direction + " " + action + " exercised its scroll button");
  ok(!nextOutsideCrumb(container, buttons, direction, action),
     direction + " " + action + " reaches the end of the trail");
  for (let crumb of buttons) {
    ok(visited.has(crumb), direction + " " + action + " fully displayed " +
       crumb.getAttribute("title"));
  }
}

function recordVisibleCrumbs(container, buttons, visited) {
  for (let button of buttons) {
    if (isElementInScrollport(container, button)) {
      visited.add(button);
    }
  }
}

function nextOutsideCrumb(container, buttons, direction, action) {
  let port = getScrollport(container);
  let ordered = action == "start" ? buttons.slice().reverse() : buttons;
  let outsideLeft = (action == "start") == (direction == "ltr");
  for (let button of ordered) {
    let rect = button.getBoundingClientRect();
    if (outsideLeft ? rect.left < port.left : rect.right > port.right) {
      return button;
    }
  }
  return null;
}

function getScrollport(container) {
  let rect = container.getBoundingClientRect();
  let style = container.ownerDocument.defaultView.getComputedStyle(container);
  let left = rect.left + parseFloat(style.borderLeftWidth);
  let right = rect.right - parseFloat(style.borderRightWidth);
  let top = rect.top + parseFloat(style.borderTopWidth);
  let bottom = rect.bottom - parseFloat(style.borderBottomWidth);
  return {left, right, top, bottom, width: right - left};
}

function isElementInScrollport(container, element) {
  let rect = element.getBoundingClientRect();
  let port = getScrollport(container);
  return rect.left >= port.left && rect.right <= port.right &&
         rect.top >= port.top && rect.bottom <= port.bottom;
}

registerCleanupFunction(function () {
  Services.prefs.clearUserPref("devtools.toolbox.host");
});
