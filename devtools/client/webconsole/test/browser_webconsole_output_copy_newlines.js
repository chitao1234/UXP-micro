/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Test that multiple messages are copied into the clipboard and that they are
// separated by new lines. See bug 916997.

"use strict";

add_task(function* () {
  const TEST_URI = "data:text/html;charset=utf8,<p>hello world, bug 916997";
  let clipboardValue = "";

  yield loadTab(TEST_URI);
  let hud = yield openConsole();
  hud.jsterm.clearOutput();

  let controller = top.document.commandDispatcher
                   .getControllerForCommand("cmd_copy");
  is(controller.isCommandEnabled("cmd_copy"), false, "cmd_copy is disabled");

  // Console evaluation intentionally hides the "debugger eval code" source
  // location. Test message separators independently of source-link layout.
  yield hud.jsterm.requestEvaluation(
    'console.log("Hello world! bug916997a");' +
    'console.log("Hello world 2! bug916997b");');

  yield waitForMessages({
    webconsole: hud,
    messages: [{
      text: "Hello world! bug916997a",
      category: CATEGORY_WEBDEV,
      severity: SEVERITY_LOG,
    }, {
      text: "Hello world 2! bug916997b",
      category: CATEGORY_WEBDEV,
      severity: SEVERITY_LOG,
    }],
  });

  is(hud.outputNode.querySelectorAll(".message-location").length, 0,
     "The fixture messages have no source-location lines");
  hud.ui.output.selectAllMessages();
  hud.outputNode.focus();

  goUpdateCommand("cmd_copy");
  controller = top.document.commandDispatcher
               .getControllerForCommand("cmd_copy");
  is(controller.isCommandEnabled("cmd_copy"), true, "cmd_copy is enabled");

  let selection = hud.iframeWindow.getSelection() + "";
  info("selection '" + selection + "'");

  yield waitForClipboardPromise(
    () => {
      goDoCommand("cmd_copy");
    },
    (str) => {
      clipboardValue = str;
      return str.indexOf("bug916997a") > -1 && str.indexOf("bug916997b") > -1;
    });

  info("clipboard value '" + clipboardValue + "'");
  let lines = clipboardValue.trim().split("\n");
  is(hud.outputNode.children.length, 2, "number of messages");
  is(lines.length, hud.outputNode.children.length, "number of lines");
  isnot(lines[0].indexOf("bug916997a"), -1,
        "first message text includes 'bug916997a'");
  isnot(lines[1].indexOf("bug916997b"), -1,
        "second message text includes 'bug916997b'");
  is(lines[0].indexOf("bug916997b"), -1,
     "first message text does not include 'bug916997b'");
});
