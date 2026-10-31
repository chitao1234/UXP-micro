/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Check that if an animation has had its playbackRate changed via the DOM, then
// the timeline UI shows the right delay and duration.
// Indeed, the header in the timeline UI always shows the unaltered time,
// because there might be multiple animations displayed at the same time, some
// of which may have a different rate than others. Those that have had their
// rate changed have a delay = delay/rate and a duration = duration/rate.

add_task(function* () {
  yield addTab(URL_ROOT + "doc_modify_playbackRate.html");

  let {panel, controller} = yield openAnimationInspector();

  let timelineEl = panel.animationsTimelineComponent.rootWrapperEl;

  let timeBlocks = timelineEl.querySelectorAll(".time-block");
  is(timeBlocks.length, 2, "2 animations are displayed");
  is(controller.animationPlayers[0].state.playbackRate, 1,
    "The first animation keeps its default playback rate");
  is(controller.animationPlayers[1].state.playbackRate, 2,
    "The fixture changed the second animation's playback rate");

  info("The first animation has its rate set to 1, let's measure it");

  let el = timeBlocks[0];
  let duration = getDurationWidth(el);
  let delay = el.querySelector(".delay").getBoundingClientRect().width;

  info("The second animation has its rate set to 2, so should be shorter");

  let el2 = timeBlocks[1];
  let duration2 = getDurationWidth(el2);
  let delay2 = el2.querySelector(".delay").getBoundingClientRect().width;

  // The width are calculated by the animation-inspector dynamically depending
  // on the size of the panel, and therefore depends on the test machine/OS.
  // Let's not try to be too precise here and compare numbers.
  ok(Number.isFinite(duration) && duration > 0 &&
     Number.isFinite(duration2) && duration2 > 0,
     "Both duration paths have a finite positive width");
  ok(Number.isFinite(delay) && delay > 0 &&
     Number.isFinite(delay2) && delay2 > 0,
     "Both delays have a finite positive width");
  let durationDelta = Math.abs((2 * duration2) - duration);
  info(`Duration widths: ${duration}, ${duration2}; delay widths: ${delay}, ${delay2}`);
  ok(durationDelta <= 1, "The duration width is correct");
  let delayDelta = Math.abs((2 * delay2) - delay);
  ok(delayDelta <= 1, "The delay width is correct");
});

function getDurationWidth(timeBlock) {
  let path = timeBlock.querySelector(".iteration-path");
  // Client rects include the graph's non-scaling stroke, whose fixed pixel
  // bounds must not be divided by playback rate. Project only the fill bounds
  // through the SVG viewBox and transforms to measure the duration itself.
  let box = path.getBBox();
  let matrix = path.getScreenCTM();
  return Math.abs(matrix.a) * box.width + Math.abs(matrix.c) * box.height;
}
