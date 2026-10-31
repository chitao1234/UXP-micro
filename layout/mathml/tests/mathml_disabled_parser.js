/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

async function checkMathMLParserPolicy(baseURL, privileged) {
  const mathNS = "http://www.w3.org/1998/Math/MathML";
  let frames = [];
  let enabledSamples = [];
  try {
    for (let disabled of [false, true, false]) {
      await SpecialPowers.pushPrefEnv({set: [["mathml.disabled", disabled]]});
      for (let extension of ["html", "xhtml"]) {
        let frame = document.createElementNS(
          "http://www.w3.org/1999/xhtml", "iframe");
        frames.push(frame);
        let loaded = new Promise(resolve => {
          frame.addEventListener("load", function onLoad(event) {
            if (event.target !== frame ||
                frame.contentDocument.documentURI === "about:blank") {
              return;
            }
            frame.removeEventListener("load", onLoad);
            resolve();
          });
        });
        frame.src = baseURL + "file_disabled_parser." + extension;
        document.body.appendChild(frame);
        await loaded;
        let doc = frame.contentDocument;
        let win = frame.contentWindow;
        let enabled = privileged || !disabled;
        let label = extension + " disabled=" + disabled +
                    " privileged=" + privileged;
        function check(element, name) {
          is(element.namespaceURI, mathNS, label + ": " + name + " keeps its public namespace");
          is(element instanceof win.MathMLElement, enabled,
             label + ": " + name + " uses the expected native element class");
        }
        check(doc.getElementById("math"), "parsed math");
        check(doc.getElementById("fraction"), "parsed fraction");
        check(doc.createElementNS(mathNS, "math"), "DOM-created math");
        check(doc.createElementNS(mathNS, "m:math"), "prefixed DOM-created math");
        let holder = doc.createElementNS("http://www.w3.org/1999/xhtml", "div");
        holder.innerHTML = '<math xmlns="' + mathNS + '"><mi>x</mi></math>';
        check(holder.firstElementChild, "fragment-parsed math");
        if (disabled) {
          for (let sample of enabledSamples) {
            ok(sample.element.cloneNode(true) instanceof sample.win.MathMLElement,
               label + ": cloning an existing enabled element preserves its class");
          }
        } else {
          enabledSamples.push({element: doc.getElementById("math"), win});
        }
      }
    }
  } finally {
    for (let frame of frames) {
      frame.remove();
    }
  }
}
