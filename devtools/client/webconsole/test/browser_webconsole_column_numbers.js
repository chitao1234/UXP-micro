/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

 // Check if console provides the right column number alongside line number

"use strict";

const TEST_URI = "http://example.com/browser/devtools/client/webconsole/" +
                 "test/test-console-column.html";

var hud;

function test() {
  loadTab(TEST_URI).then(() => {
    openConsole().then(consoleOpened);
  });
}

function consoleOpened(aHud) {
  hud = aHud;

  waitForMessages({
    webconsole: hud,
    messages: [{
      text: "Error Message",
      category: CATEGORY_WEBDEV,
      severity: SEVERITY_ERROR
    }]
  }).then(testLocationColumn);
}

function testLocationColumn() {
  let expected = [
    { text: "INLINE SCRIPT:", location: "10:15" },
    { text: "Further", location: "10:47" },
    { text: "I'm warning you", location: "11:17" },
    { text: "Error Message", location: "12:19" },
    { text: "Rainbooooww", location: "13:17" },
    { text: "NYAN CATZ", location: "14:15" },
  ];

  for (let item of expected) {
    let message = [...hud.outputNode.children].find(node =>
      node.textContent.includes(item.text));
    ok(message, "Found console message " + item.text);
    if (message) {
      ok(message.textContent.includes(item.location),
         "Found expected line:column " + item.location + " in: " +
         message.textContent);
    }
  }

  finishTest();
}
