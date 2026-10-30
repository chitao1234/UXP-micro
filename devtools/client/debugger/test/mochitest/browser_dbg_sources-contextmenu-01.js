/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests the "Copy URL" functionality of the sources panel context menu
 */

const TAB_URL = EXAMPLE_URL + "doc_function-search.html";
const SCRIPT_URI = EXAMPLE_URL + "code_function-search-01.js";

function test() {
  let gTab, gPanel, gDebugger, gSources;

  let options = {
    source: SCRIPT_URI,
    line: 1
  };
  initDebugger(TAB_URL, options).then(([aTab,, aPanel]) => {
    gTab = aTab;
    gPanel = aPanel;
    gDebugger = gPanel.panelWin;
    gSources = gDebugger.DebuggerView.Sources;

    openContextMenu()
      .then(testCopyMenuItem)
      .then(() => closeDebuggerAndFinish(gPanel))
      .then(null, aError => {
        ok(false, "Got an error: " + aError.message + "\n" + aError.stack);
        return closeDebuggerAndFinish(gPanel);
      });
  });

  function clickCopyURL() {
    return new Promise((resolve, reject) => {
      let copyURLMenuItem = gDebugger.document.getElementById("debugger-sources-context-copyurl");
      if (!copyURLMenuItem) {
        reject(new Error("The Copy URL context menu item is not available."));
      }

      ok(copyURLMenuItem, "The Copy URL context menu item is available.");
      EventUtils.synthesizeMouseAtCenter(copyURLMenuItem, {}, gDebugger);
      resolve();
    });
  }

  function testCopyMenuItem() {
    return waitForClipboardPromise(clickCopyURL, SCRIPT_URI);
  }

  function openContextMenu() {
    let debuggerDocument = gDebugger.document;
    let contextMenu = debuggerDocument.getElementById("debuggerSourcesContextMenu");
    let sourceNode = gSources.selectedItem.prebuiltNode;
    // Source selection does not guarantee that the row is in the viewport.
    // Match the newer debugger's rightClickEl helper before synthesizing input.
    sourceNode.scrollIntoView();
    return new Promise(resolve => gDebugger.requestAnimationFrame(resolve)).then(() => {
      let rect = sourceNode.getBoundingClientRect();
      let hit = debuggerDocument.elementFromPoint(
        rect.left + rect.width / 2, rect.top + rect.height / 2);
      let hitsSource = hit && (hit === sourceNode || sourceNode.contains(hit));
      let details = JSON.stringify({
        rect: { left: rect.left, top: rect.top, width: rect.width, height: rect.height },
        viewport: { width: gDebugger.innerWidth, height: gDebugger.innerHeight },
        hit: hit && { name: hit.nodeName, id: hit.id }
      });
      ok(hitsSource, "The source entry center receives input: " + details);
      if (!hitsSource) {
        throw new Error("The source entry center is not reachable: " + details);
      }
      let contextMenuShown = once(contextMenu, "popupshown");
      EventUtils.synthesizeMouseAtCenter(sourceNode, {type: "contextmenu"}, gDebugger);
      return contextMenuShown;
    });
  }
}
