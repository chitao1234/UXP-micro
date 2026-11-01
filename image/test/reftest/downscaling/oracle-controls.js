/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

function getTestCases() {
  var cases = [];
  function check(pass, detail) {
    cases.push({testPassed: () => pass, testDescription: () => detail});
  }
  function cssColor(value) {
    // Gecko52 may serialize these fixed fixture colors by CSS keyword rather
    // than rgb()/rgba(). Use their specified RGBA values; unknown colors still
    // fail instead of supplying a permissive fallback or changing expectations.
    var namedColors = {
      transparent: [0, 0, 0, 0],
      black: [0, 0, 0, 255],
      red: [255, 0, 0, 255],
      lime: [0, 255, 0, 255]
    };
    var keyword = value.trim().toLowerCase();
    if (Object.prototype.hasOwnProperty.call(namedColors, keyword)) {
      return namedColors[keyword];
    }
    var match = /^rgba?\(([^)]+)\)$/.exec(value);
    if (!match) {
      throw new Error("Unexpected computed CSS color " + value);
    }
    var parts = match[1].split(",").map(Number);
    return [parts[0], parts[1], parts[2], parts.length === 4 ? Math.round(parts[3] * 255) : 255];
  }
  function reference(kind, size, origin, expectedRects) {
    var frame = document.getElementById(kind + "-frame");
    var win = frame.contentWindow;
    var box = win.document.getElementById("oracle");
    var r = box.getBoundingClientRect();
    var au = value => Math.round(value * 60);
    check(au(r.left) === origin * 60 && au(r.top) === origin * 60 &&
          au(r.width) === size * 60 && au(r.height) === size * 60,
          kind + ": CSS bounds/origin retain the original fixture contract");
    check(win.rasterCSSOraclePremise && win.rasterCSSOraclePremise.pass,
          kind + ": independent reference load passed its CSS premise");
    var result = [{id: "background", left: 0, top: 0, width: size, height: size,
                   color: cssColor(win.getComputedStyle(box).backgroundColor)}];
    for (var child of box.children) {
      var c = child.getBoundingClientRect();
      var item = {id: child.id, left: (au(c.left) - au(r.left)) / 60,
        top: (au(c.top) - au(r.top)) / 60, width: au(c.width) / 60,
        height: au(c.height) / 60,
        color: cssColor(win.getComputedStyle(child).backgroundColor)};
      var expected = expectedRects[child.id];
      check(expected && item.left === expected[0] && item.top === expected[1] &&
            item.width === expected[2] && item.height === expected[3] &&
            item.color.join(",") === expected.slice(4).join(","),
            kind + ": independent source geometry/color for " + child.id);
      result.push(item);
    }
    check(result.length === Object.keys(expectedRects).length + 1,
          kind + ": no missing or additional CSS primitives");
    return result;
  }
  function rawGolden(id, size) {
    var img = document.getElementById(id);
    check(img.complete && img.naturalWidth === size && img.naturalHeight === size,
          id + ": original golden has its exact1x dimensions");
    var canvas = document.createElement("canvas");
    canvas.width = canvas.height = size;
    var ctx = canvas.getContext("2d");
    // Identity mapping into an unscaled pixel buffer: native window DPR cannot
    // insert the 20->30 or32->48 reference-image upsampling under investigation.
    ctx.drawImage(img, 0, 0);
    return ctx.getImageData(0, 0, size, size).data;
  }
  function mismatches(rectangles, golden, size) {
    var count = 0;
    for (var y = 0; y < size; ++y) {
      for (var x = 0; x < size; ++x) {
        var expected = [0, 0, 0, 0];
        for (var rect of rectangles) {
          if (x + 0.5 >= rect.left && x + 0.5 < rect.left + rect.width &&
              y + 0.5 >= rect.top && y + 0.5 < rect.top + rect.height &&
              rect.color[3] !== 0) {
            expected = rect.color;
          }
        }
        var offset = (y * size + x) * 4;
        if (expected.some((value, channel) => value !== golden[offset + channel])) {
          ++count;
        }
      }
    }
    return count;
  }

  var l = reference("l", 20, 8, {
    right: [16, 0, 4, 20, 0, 0, 0, 255],
    bottom: [0, 16, 20, 4, 0, 0, 0, 255]
  });
  var split = reference("split", 32, 0, {
    right: [16, 0, 16, 32, 255, 0, 0, 255]
  });
  check(l[0].color.join(",") === "0,0,0,0", "L interior remains transparent");
  check(split[0].color.join(",") === "0,255,0,255", "Split left half is lime");
  var lGolden = rawGolden("l-golden", 20);
  var splitGolden = rawGolden("split-golden", 32);
  check(mismatches(l, lGolden, 20) === 0,
        "CSS L primitives exactly match every original20x20 golden RGBA pixel at1x");
  check(mismatches(split, splitGolden, 32) === 0,
        "CSS split primitives exactly match every original32x32 golden RGBA pixel at1x");
  check(mismatches([], splitGolden, 32) === 1024,
        "Blank32 control disagrees at all1024 golden pixels");
  check(mismatches(split.filter(r => r.id !== "right"), splitGolden, 32) === 512,
        "Wrong-half control disagrees at exactly512 golden pixels");
  check(mismatches(l.filter(r => r.id !== "right"), lGolden, 20) === 64,
        "Missing right edge disagrees at exactly64 golden pixels");
  check(mismatches(l.filter(r => r.id !== "bottom"), lGolden, 20) === 64,
        "Missing bottom edge disagrees at exactly64 golden pixels");

  // This oracle uses the ORIGINAL JPEG decoded at its native256x256 size.
  // Constants are from the independent source decode recorded in
  // jpeg-source-evidence.json; the downscaled browser image is never sampled.
  var jpeg = reference("jpeg", 32, 0, {
    right: [16, 0, 16, 32, 254, 0, 0, 255]
  });
  check(jpeg[0].color.join(",") === "0,255,1,255",
        "JPEG CSS left half equals the independently decoded source plateau");
  var jpegSource = rawGolden("jpeg-golden", 256);
  var nativePlateauMismatch = 0;
  var nativeEdgeMismatch = 0;
  var nativeAlphaMismatch = 0;
  var plateauPixels = 0;
  var edgePixels = 0;
  var leftPixels = 0;
  var rightPixels = 0;
  for (var y = 0; y < 256; ++y) {
    for (var x = 0; x < 256; ++x) {
      var offset = (y * 256 + x) * 4;
      var expected = x < 128 ? [0, 255, 1, 255] : [254, 0, 0, 255];
      var mismatch = expected.some((value, channel) =>
                                  jpegSource[offset + channel] !== value);
      if (jpegSource[offset + 3] !== 255) {
        ++nativeAlphaMismatch;
      }
      // Explicitly separate the four native SOURCE boundary columns from
      // plateau color checks. No filtered32px/48px output is used here.
      if (x >= 126 && x <= 129) {
        ++edgePixels;
        nativeEdgeMismatch += mismatch ? 1 : 0;
      } else {
        ++plateauPixels;
        nativePlateauMismatch += mismatch ? 1 : 0;
      }
      if (!mismatch) {
        if (x < 128) {
          ++leftPixels;
        } else {
          ++rightPixels;
        }
      }
    }
  }
  check(plateauPixels === 64512 && nativePlateauMismatch === 0,
        "Native1x JPEG plateaus match source RGB exactly; source columns126..129 excluded explicitly");
  check(edgePixels === 1024 && nativeEdgeMismatch === 0,
        "Native1x JPEG boundary strip separately matches the original source half model");
  check(nativeAlphaMismatch === 0,
        "Native1x JPEG is fully opaque at all65536 source pixels");
  check(leftPixels === 32768 && rightPixels === 32768,
        "Native1x JPEG has exactly32768 pixels of each independently decoded source color");
  var jpegModel = new Uint8ClampedArray(32 * 32 * 4);
  for (var y = 0; y < 32; ++y) {
    for (var x = 0; x < 32; ++x) {
      jpegModel.set(x < 16 ? [0, 255, 1, 255] : [254, 0, 0, 255],
                    (y * 32 + x) * 4);
    }
  }
  check(mismatches(jpeg, jpegModel, 32) === 0,
        "JPEG CSS primitives exactly match the independently source-derived32px half model");
  check(mismatches([], jpegModel, 32) === 1024,
        "Blank JPEG control disagrees at all1024 source-model pixels");
  check(mismatches(jpeg.filter(r => r.id !== "right"), jpegModel, 32) === 512,
        "Wrong-half JPEG control disagrees at exactly512 source-model pixels");
  dump("RASTER-JPEG-SOURCE-CONTROL " + JSON.stringify({
    size: 256, leftRGBA: [0, 255, 1, 255], rightRGBA: [254, 0, 0, 255],
    plateauExcludedSourceColumns: [126, 127, 128, 129],
    plateauPixels, edgePixels, nativePlateauMismatch, nativeEdgeMismatch,
    nativeAlphaMismatch, leftPixels, rightPixels,
    independentDerivation: "Original native-size JPEG; never downscaled output"
  }) + "\n");

  return cases;
}
