/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// MathMLElement implements ElementCSSInlineStyle, so its stylesheet and inline
// declarations must remain inspectable just like the surrounding HTML.
// Adapted from this test in Firefox 309a5a015112: preserve its five property
// checks while using this harness's generator-based tasks.
const TEST_URI = `
  <style>
    div { background-color: hotpink; }
    math { font-size: 36px; }
    msubsup { background-color: tomato; }
  </style>
  <div>
    <math xmlns="http://www.w3.org/1998/Math/MathML">
      <mfrac>
        <msubsup><mi>a</mi><mi>i</mi><mi>j</mi></msubsup>
        <msub><mi>x</mi><mn style="color: gold">0</mn></msub>
      </mfrac>
    </math>
  </div>
`;

add_task(function* () {
  yield addTab("data:text/html;charset=utf-8," + encodeURIComponent(TEST_URI));
  let {inspector, view} = yield openRuleView();
  for (let [node, selector, property, value] of [
    ["div", "div", "background-color", "hotpink"],
    ["math", "math", "font-size", "36px"],
    ["msubsup", "msubsup", "background-color", "tomato"],
    ["mn", "element", "color", "gold"],
    ["div", "div", "background-color", "hotpink"]
  ]) {
    yield selectNode(node, inspector);
    is(getRuleViewPropertyValue(view, selector, property), value,
       node + " exposes the expected " + property + " declaration");
  }
});
