/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests that the source tree works.

function* waitForSourceCount(dbg, i) {
  // We are forced to wait until the DOM nodes appear because the
  // source tree batches its rendering.
  let lastCount = -1;
  yield waitUntil(() => {
    let nodes = findAllElements(dbg, "sourceNodes");
    if (nodes.length !== lastCount) {
      info("Waiting for rendered source rows: " + JSON.stringify({
        expected: i,
        actual: nodes.length,
        rows: Array.from(nodes, node => node.textContent)
      }));
      lastCount = nodes.length;
    }
    return nodes.length === i;
  });
}

add_task(function* () {
  const dbg = yield initDebugger("doc-sources.html");
  const { selectors: { getSelectedSource }, getState } = dbg;

  // Expand nodes and make sure more sources appear.
  yield waitForSources(dbg, "simple1.js", "simple2.js", "long.js",
                       "doc-sources.html", "nested-source.js");
  yield waitForSourceCount(dbg, 2);
  is(findAllElements(dbg, "sourceNodes").length, 2);

  clickElement(dbg, "sourceArrow", 2);
  yield waitForSourceCount(dbg, 7);
  is(findAllElements(dbg, "sourceNodes").length, 7);

  clickElement(dbg, "sourceArrow", 3);
  yield waitForSourceCount(dbg, 8);
  is(findAllElements(dbg, "sourceNodes").length, 8);

  // Select a source.
  ok(!findElementWithSelector(dbg, ".sources-list .focused"),
     "Source is not focused");
  const selected = waitForDispatch(dbg, "SELECT_SOURCE");
  clickElement(dbg, "sourceNode", 4);
  yield selected;
  ok(findElementWithSelector(dbg, ".sources-list .focused"),
     "Source is focused");
  ok(getSelectedSource(getState()).get("url").includes("nested-source.js"),
     "The right source is selected");

  // Make sure new sources appear in the list.
  ContentTask.spawn(gBrowser.selectedBrowser, null, function() {
    const script = content.document.createElement("script");
    script.src = "math.min.js";
    content.document.body.appendChild(script);
  });

  yield waitForSourceCount(dbg, 9);
  is(findElement(dbg, "sourceNode", 7).querySelector("span").innerText,
     "math.min.js",
     "The dynamic script exists");

  // Make sure named eval sources appear in the list.
  ContentTask.spawn(gBrowser.selectedBrowser, null, function() {
    content.eval("window.evaledFunc = function() {} //# sourceURL=evaled.js");
  });
  yield waitForSourceCount(dbg, 11);
  is(findElement(dbg, "sourceNode", 2).querySelector("span").innerText,
     "evaled.js",
     "The eval script exists");
});
