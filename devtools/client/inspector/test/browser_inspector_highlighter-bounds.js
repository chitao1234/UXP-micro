/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Points on an edge's infinite line must not count as points on the segment.
// Use real layout boxes separated by exactly one native CSS appunit.
const APP_UNITS_PER_CSS_PIXEL = 60;
const ONE_APP_UNIT = 1 / APP_UNITS_PER_CSS_PIXEL;
const TEST_URI = "data:text/html;charset=utf-8," + encodeURIComponent(`
  <style>
    div { position: absolute; width: 20px; height: 20px; }
    #target { left: 20px; top: 20px; }
    #horizontal { left: ${20 + ONE_APP_UNIT}px; top: 20px; }
    #vertical { left: 20px; top: ${20 + ONE_APP_UNIT}px; }
    #diagonal {
      left: ${20 + ONE_APP_UNIT}px;
      top: ${20 + ONE_APP_UNIT}px;
    }
    #diagonal-target {
      left: 100px;
      top: 100px;
      transform: matrix(1, 1, 0, 1, 0, 0);
      transform-origin: 0 0;
    }
    .point { width: 0; height: 0; }
    #diagonal-on-edge { left: 100px; top: 100px; }
    #diagonal-beyond-edge {
      left: ${100 - ONE_APP_UNIT}px;
      top: ${100 - ONE_APP_UNIT}px;
    }
    #sub-appunit-target {
      left: 20px;
      top: 180px;
      transform: translateX(${ONE_APP_UNIT / 4}px);
    }
    #sub-appunit-probe { left: 20px; top: 180px; }
  </style>
  <div id="target"></div>
  <div id="horizontal"></div>
  <div id="vertical"></div>
  <div id="diagonal"></div>
  <div id="diagonal-target"></div>
  <div id="diagonal-on-edge" class="point"></div>
  <div id="diagonal-beyond-edge" class="point"></div>
  <div id="sub-appunit-target"></div>
  <div id="sub-appunit-probe" class="point"></div>
`);

add_task(function* () {
  let {inspector, testActor} = yield openInspectorForURL(TEST_URI);
  let target = yield getNodeFront("#target", inspector);
  yield inspector.toolbox.highlighterUtils.highlightNodeFront(target);

  ok((yield testActor.assertHighlightedNode("#target")),
     "The highlighted rectangle matches its real layout box");

  let targetRect = yield testActor.getNodeRect("#target");
  for (let {selector, dx, dy} of [
    {selector: "#horizontal", dx: 1, dy: 0},
    {selector: "#vertical", dx: 0, dy: 1},
    {selector: "#diagonal", dx: 1, dy: 1}
  ]) {
    let rect = yield testActor.getNodeRect(selector);
    is(Math.round(rect.left * APP_UNITS_PER_CSS_PIXEL) -
       Math.round(targetRect.left * APP_UNITS_PER_CSS_PIXEL), dx,
       selector + " has the intended horizontal appunit separation");
    is(Math.round(rect.top * APP_UNITS_PER_CSS_PIXEL) -
       Math.round(targetRect.top * APP_UNITS_PER_CSS_PIXEL), dy,
       selector + " has the intended vertical appunit separation");
    ok(!(yield testActor.assertHighlightedNode(selector)),
       selector + " is not the highlighted rectangle");
  }

  target = yield getNodeFront("#diagonal-target", inspector);
  yield inspector.toolbox.highlighterUtils.highlightNodeFront(target);
  yield testActor.isNodeCorrectlyHighlighted("#diagonal-target", is,
                                            "exact diagonal border");

  ok((yield testActor.assertHighlightedNode("#diagonal-on-edge")),
     "A real point at the diagonal segment endpoint is inside");

  let endpoint = yield testActor.getNodeRect("#diagonal-on-edge");
  let beyond = yield testActor.getNodeRect("#diagonal-beyond-edge");
  is(beyond.left, beyond.top,
     "The outside point lies exactly on the diagonal edge's infinite line");
  is(Math.round(endpoint.left * APP_UNITS_PER_CSS_PIXEL) -
     Math.round(beyond.left * APP_UNITS_PER_CSS_PIXEL), 1,
     "The outside point is one appunit before the diagonal endpoint");
  ok(!(yield testActor.assertHighlightedNode("#diagonal-beyond-edge")),
     "A point beyond the diagonal segment endpoint is outside");

  target = yield getNodeFront("#sub-appunit-target", inspector);
  yield inspector.toolbox.highlighterUtils.highlightNodeFront(target);
  yield testActor.isNodeCorrectlyHighlighted("#sub-appunit-target", is,
                                            "sub-appunit transform");
  let regions = yield testActor.getAllAdjustedQuads("#sub-appunit-target");
  let probe = yield testActor.getNodeRect("#sub-appunit-probe");
  ok(regions.border[0].p1.x > probe.left &&
     regions.border[0].p1.x < probe.left + ONE_APP_UNIT / 2,
     "The real transformed edge is less than half an appunit beyond the probe");
  ok(!(yield testActor.assertHighlightedNode("#sub-appunit-probe")),
     "The exact containment check preserves a genuine sub-appunit difference");

  yield inspector.toolbox.highlighterUtils.unhighlight();
});
