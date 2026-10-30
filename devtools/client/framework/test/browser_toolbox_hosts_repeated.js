/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const {Toolbox} = require("devtools/client/framework/toolbox");
const {SIDE, BOTTOM, WINDOW} = Toolbox.HostType;

const URL = "data:text/html;charset=utf8,browser_toolbox_hosts_repeated.js";

add_task(function* () {
  info("Create a test tab and open the toolbox");
  let tab = yield addTab(URL);
  let target = TargetFactory.forTab(tab);
  let toolbox = yield gDevTools.showToolbox(target, "webconsole");

  yield changeToolboxHost(toolbox);
  yield toolbox.destroy();

  toolbox = target = null;
  gBrowser.removeCurrentTab();
});

function* changeToolboxHost(toolbox) {
  // Upstream c83282fd4d removed this fixture's telemetry assertions. Retain
  // its repeated host changes as real lifecycle coverage with explicit checks.
  for (let host of [SIDE, WINDOW, BOTTOM, SIDE, WINDOW, BOTTOM]) {
    info("Switch toolbox host to " + host);
    yield toolbox.switchHost(host);
    is(toolbox.hostType, host, "The requested host is active");
    is(Services.prefs.getCharPref("devtools.toolbox.host"), host,
       "The active host preference follows each transition");
    is(toolbox.currentToolId, "webconsole", "Host changes preserve the selected tool");
    ok(toolbox.doc.defaultView, "The toolbox document remains attached");
  }
}
