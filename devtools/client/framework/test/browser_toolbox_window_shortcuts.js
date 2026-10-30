/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

var {Toolbox} = require("devtools/client/framework/toolbox");

var toolbox, toolIDs, idIndex, modifiedPrefs = [];

function test() {
  addTab("about:blank").then(function () {
    let target = TargetFactory.forTab(gBrowser.selectedTab);
    // Tool support can depend on the actors advertised by the target.
    return target.makeRemote().then(() => target);
  }).then(function (target) {
    toolIDs = [];
    for (let [id, definition] of gDevTools._tools) {
      if (definition.key) {
        if (!definition.isTargetSupported(target)) {
          info("No shortcut test for unsupported tool: " + id);
          continue;
        }
        toolIDs.push(id);

        // Enable disabled tools
        let pref = definition.visibilityswitch, prefValue;
        try {
          prefValue = Services.prefs.getBoolPref(pref);
        } catch (e) {
          continue;
        }
        if (!prefValue) {
          modifiedPrefs.push(pref);
          Services.prefs.setBoolPref(pref, true);
        }
      }
    }
    ok(toolIDs.length > 1, "Multiple supported tools have shortcuts to test");
    idIndex = 0;
    return gDevTools.showToolbox(target, toolIDs[0], Toolbox.HostType.WINDOW);
  }).then(testShortcuts);
}

function testShortcuts(aToolbox, aIndex) {
  if (aIndex === undefined) {
    aIndex = 1;
  } else if (aIndex == toolIDs.length) {
    tidyUp();
    return;
  }

  toolbox = aToolbox;
  info("Toolbox fired a `ready` event");

  toolbox.once("select", selectCB);

  let key = gDevTools._tools.get(toolIDs[aIndex]).key;
  let toolModifiers = gDevTools._tools.get(toolIDs[aIndex]).modifiers;
  let modifiers = {
    accelKey: toolModifiers.includes("accel"),
    altKey: toolModifiers.includes("alt"),
    shiftKey: toolModifiers.includes("shift"),
  };
  idIndex = aIndex;
  info("Testing shortcut for tool " + aIndex + ":" + toolIDs[aIndex] +
       " using key " + key);
  EventUtils.synthesizeKey(key, modifiers, toolbox.win.parent);
}

function selectCB(event, id) {
  info("toolbox-select event from " + id);

  is(toolIDs.indexOf(id), idIndex,
     "Correct tool is selected on pressing the shortcut for " + id);

  testShortcuts(toolbox, idIndex + 1);
}

function tidyUp() {
  toolbox.destroy().then(function () {
    gBrowser.removeCurrentTab();

    for (let pref of modifiedPrefs) {
      Services.prefs.clearUserPref(pref);
    }
    toolbox = toolIDs = idIndex = modifiedPrefs = Toolbox = null;
    finish();
  });
}
