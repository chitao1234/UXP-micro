/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(function* () {
  // ac25827a87 removed the SPS profiler. The old test requested performance,
  // silently exercised the console fallback, and only checked generic metrics.
  // Test the supported fallback and its full destruction/recreation directly.
  let performanceDefinition = gDevTools.getToolDefinition("performance");
  ok(performanceDefinition, "The tool definition remains available for capability checks");
  let tab = yield addTab("data:text/html;charset=utf-8,<p>Unsupported tool fallback</p>");
  let target;
  try {
    for (let i = 0; i < 2; i++) {
      target = TargetFactory.forTab(tab);
      yield target.makeRemote();
      ok(!performanceDefinition.isTargetSupported(target),
         "The target does not expose the removed profiler backend");
      let toolbox = yield gDevTools.showToolbox(target, "performance");
      is(toolbox.currentToolId, "webconsole", "An unavailable initial tool falls back to the console");
      ok(toolbox.getPanel("webconsole"), "The fallback panel is loaded");
      ok(!toolbox.getPanel("performance"), "No removed-tool panel was created");
      ok(!toolbox.doc.getElementById("toolbox-tab-performance"), "No removed-tool tab is exposed");
      is(gDevTools.getToolbox(target), toolbox, "The fallback toolbox is registered");
      yield gDevTools.closeToolbox(target);
      ok(!gDevTools.getToolbox(target), "Closing removes the fallback toolbox");
    }
  } finally {
    if (target && gDevTools.getToolbox(target)) {
      yield gDevTools.closeToolbox(target);
    }
    gBrowser.removeTab(tab);
  }
});
