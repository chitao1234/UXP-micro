/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const URI = "chrome://mochitests/content/browser/devtools/client/" +
            "sourceeditor/test/codemirror/codemirror.html";
loadHelperScript("helper_codemirror_runner.js");

function test() {
  requestLongerTimeout(3);
  waitForExplicitFinish();

  addTab(URI).then(function (tab) {
    // The unfiltered declared scripts register 246 cases. A second driver.js
    // initialization can silently erase 162 core/comment/document cases.
    runCodeMirrorTest(tab.linkedBrowser, {
      minimumTests: 246,
      requiredTests: ["core_fromTextArea", "comment_block", "doc_basic"]
    });
  });
}
