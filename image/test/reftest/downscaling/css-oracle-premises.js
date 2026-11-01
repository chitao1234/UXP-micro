/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

function installCSSOraclePremise(size, origin, role, selector, releaseWait) {
  window.addEventListener("load", function checkCSSOraclePremise() {
    var nodes = document.querySelectorAll(selector);
    var node = nodes[0];
    var r = node ? node.getBoundingClientRect() : null;
    var toAU = value => Math.round(value * 60);
    var appunits = r ? {left: toAU(r.left), top: toAU(r.top),
                        width: toAU(r.width), height: toAU(r.height)} : null;
    var dpr = window.devicePixelRatio;
    var natural = node && node.localName === "img" ?
      {width: node.naturalWidth, height: node.naturalHeight, complete: node.complete} : null;
    var reason = nodes.length !== 1 ? "wrong-box-count" :
      !Number.isFinite(dpr) || dpr <= 0 ? "invalid-DPR" :
      natural && (!natural.complete || !natural.width || !natural.height) ? "image-incomplete" :
      appunits.left !== origin * 60 || appunits.top !== origin * 60 ? "CSS-origin-mismatch" :
      appunits.width !== size * 60 || appunits.height !== size * 60 ? "CSS-size-mismatch" :
      window.scrollX !== 0 || window.scrollY !== 0 ? "unexpected-scroll" : null;
    var apd = Number.isFinite(dpr) && dpr > 0 ? Math.max(1, Math.round(60 / dpr)) : null;
    var record = {role, url: location.href, expectedCSSSize: size,
      expectedCSSOrigin: origin, appUnitsPerCSSPixel: 60,
      actualAppUnits: appunits, naturalImage: natural,
      devicePixelRatio: dpr, inferredAppUnitsPerDevPixel: apd,
      nativeSizeAligned: apd !== null && size * 60 % apd === 0,
      nativeOriginAligned: apd !== null && origin * 60 % apd === 0,
      readyState: document.readyState, pass: reason === null, reason};
    window.rasterCSSOraclePremise = record;
    dump("RASTER-CSS-ORACLE-PREMISE " + JSON.stringify(record) + "\n");
    if (reason) {
      // Opposite poison colors also prevent matching failed premises on both sides.
      document.documentElement.style.background = role === "test" ? "magenta" : "cyan";
      document.body.textContent = "CSS ORACLE PREMISE FAILED: " + role;
      document.documentElement.classList.remove("reftest-wait");
      throw new Error("CSS oracle premise failed: " + JSON.stringify(record));
    }
    if (releaseWait) {
      document.documentElement.classList.remove("reftest-wait");
    }
  }, {once: true});
}
