/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

 /* Globals defined in: devtools/client/inspector/test/head.js */

"use strict";

// Test that the arrows and handlers are positioned correctly and have the right
// size.

const TEST_URL = URL_ROOT + "doc_inspector_highlighter-geometry_01.html";
const ID = "geometry-editor-";
const HIGHLIGHTER_TYPE = "GeometryEditorHighlighter";
const APP_UNITS_PER_CSS_PIXEL = 60;

const handlerMap = {
  "top": {"cx": "x2", "cy": "y2"},
  "bottom": {"cx": "x2", "cy": "y2"},
  "left": {"cx": "x2", "cy": "y2"},
  "right": {"cx": "x2", "cy": "y2"}
};

add_task(function* () {
  let helper = yield openInspectorForURL(TEST_URL)
                       .then(getHighlighterHelperFor(HIGHLIGHTER_TYPE));

  helper.prefix = ID;

  let { hide, finalize } = helper;

  // Borders snap to native device pixels. Recover integer appunits before
  // arithmetic because computed CSS serialization can round fractional widths.
  let borders = yield ContentTask.spawn(gBrowser.selectedBrowser,
    APP_UNITS_PER_CSS_PIXEL, function* (appUnitsPerCSSPixel) {
      let parent = content.getComputedStyle(
        content.document.querySelector("#absolute-container"));
      let relative = content.getComputedStyle(
        content.document.querySelector(".relative"));
      let toAppUnits = value =>
        Math.round(parseFloat(value) * appUnitsPerCSSPixel);
      return {
        parentLeft: toAppUnits(parent.borderLeftWidth),
        parentTop: toAppUnits(parent.borderTopWidth),
        relativeTop: toAppUnits(relative.borderTopWidth),
        relativeBottom: toAppUnits(relative.borderBottomWidth)
      };
    });
  let floorCSSPixels = (pixels, border) =>
    Math.floor((pixels * APP_UNITS_PER_CSS_PIXEL + border) /
               APP_UNITS_PER_CSS_PIXEL);
  let x = pixels => floorCSSPixels(pixels, borders.parentLeft);
  let y = pixels => floorCSSPixels(pixels, borders.parentTop);
  let relativeMiddleY = floorCSSPixels(95, borders.parentTop +
    (borders.relativeTop + borders.relativeBottom) / 2);

  yield checkArrowsAndHandlers(helper, ".absolute-all-4", {
    "top": {x1: x(505), y1: y(50), x2: x(505), y2: y(60)},
    "bottom": {x1: x(505), y1: y(450), x2: x(505), y2: y(250)},
    "left": {x1: x(400), y1: y(155), x2: x(410), y2: y(155)},
    "right": {x1: x(900), y1: y(155), x2: x(600), y2: y(155)}
  });

  yield checkArrowsAndHandlers(helper, ".relative", {
    "top": {x1: x(900), y1: y(50), x2: x(900), y2: y(90)},
    "left": {x1: x(400), y1: relativeMiddleY,
             x2: x(650), y2: relativeMiddleY}
  });

  yield checkArrowsAndHandlers(helper, ".fixed", {
    "top": {x1: 25, y1: 0, x2: 25, y2: 400},
    "left": {x1: 0, y1: 425, x2: 0, y2: 425}
  });

  info("Hiding the highlighter");
  yield hide();
  yield finalize();
});

function* checkArrowsAndHandlers(helper, selector, arrows) {
  info("Highlighting the test node " + selector);

  yield helper.show(selector);

  for (let side in arrows) {
    yield checkArrowAndHandler(helper, side, arrows[side]);
  }
}

function* checkArrowAndHandler({getElementAttribute}, name, expectedCoords) {
  info("Checking " + name + "arrow and handler coordinates are correct");

  let handlerX = yield getElementAttribute("handler-" + name, "cx");
  let handlerY = yield getElementAttribute("handler-" + name, "cy");

  let expectedHandlerX = yield getElementAttribute("arrow-" + name,
                                handlerMap[name].cx);
  let expectedHandlerY = yield getElementAttribute("arrow-" + name,
                                handlerMap[name].cy);

  is(handlerX, expectedHandlerX,
    "coordinate X for handler " + name + " is correct.");
  is(handlerY, expectedHandlerY,
    "coordinate Y for handler " + name + " is correct.");

  for (let coordinate in expectedCoords) {
    let value = yield getElementAttribute("arrow-" + name, coordinate);

    is(Math.floor(value), expectedCoords[coordinate],
      coordinate + " coordinate for arrow " + name + " is correct");
  }
}
