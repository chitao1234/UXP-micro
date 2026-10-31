/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

SimpleTest.waitForExplicitFinish();

const { classes: Cc, interfaces: Ci, utils: Cu } = Components;
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/Task.jsm");
Cu.import("resource://gre/modules/ctypes.jsm");

const SAMPLE = "ABC abc XYZ 0123";
const FONT_FAMILY = "ChunkFive";
const FR_PRIVATE = 0x10;

window.addEventListener("load", () => {
  Task.spawn(runTest).catch(error => {
    ok(false, "GDI synthetic-bold regression failed: " + error + "\n" + error.stack);
  }).then(() => SimpleTest.finish());
}, {once: true});

function* runTest() {
  let gfxInfo = Cc["@mozilla.org/gfx/info;1"].getService(Ci.nsIGfxInfo);
  if (gfxInfo.DWriteEnabled) {
    // This test uses process-private GDI font registration, not DirectWrite's
    // system font collection. Run its full coverage with the startup prefs
    // gfx.direct2d.disabled=true and gfx.content.azure.backends=cairo.
    todo(false, "GDI synthetic-bold coverage requires a GDI font-list profile");
    return;
  }
  is(gfxInfo.DWriteEnabled, false, "The full regression uses GDI fonts");

  // Out-of-directory support files are installed beside this chrome test.
  let fontURI = Services.io.newURI("Chunkfive.ttf", null,
                                  Services.io.newURI(document.documentURI));
  let registry = Cc["@mozilla.org/chrome/chrome-registry;1"]
                   .getService(Ci.nsIChromeRegistry);
  let fontFile = registry.convertChromeURL(fontURI).QueryInterface(Ci.nsIFileURL).file;
  let colorURI = Services.io.newURI("MicroGDIColor.ttf", null, fontURI);
  let colorFile = registry.convertChromeURL(colorURI).QueryInterface(Ci.nsIFileURL).file;
  ok(fontFile.exists(), "The deterministic single-face font fixture exists");
  ok(colorFile.exists(), "The deterministic COLR font fixture exists");

  let fontEnumerator = Cc["@mozilla.org/gfx/fontenumerator;1"]
                         .getService(Ci.nsIFontEnumerator);
  let gdi = ctypes.open("gdi32.dll");
  // INT/BOOL and DWORD remain 32-bit on both Windows ABIs; WCHAR is UTF-16.
  let addFont = gdi.declare("AddFontResourceExW", ctypes.winapi_abi, ctypes.int,
                            ctypes.jschar.ptr, ctypes.uint32_t, ctypes.voidptr_t);
  let removeFont = gdi.declare("RemoveFontResourceExW", ctypes.winapi_abi, ctypes.int,
                               ctypes.jschar.ptr, ctypes.uint32_t, ctypes.voidptr_t);
  let registered = [];
  let frame = document.getElementById("sample");
  try {
    for (let file of [fontFile, colorFile]) {
      let count = addFont(file.path, FR_PRIVATE, null);
      ok(count > 0, file.leafName + " is registered only in this browser process");
      if (!count) {
        return;
      }
      registered.push(file);
    }
    fontEnumerator.updateFontList();
    is(fontEnumerator.getStandardFamilyName(FONT_FAMILY), FONT_FAMILY,
       "The installed-family lookup resolves the actual bundled family");
    is(fontEnumerator.getStandardFamilyName("MicroGDIColor"), "MicroGDIColor",
       "The installed-family lookup resolves the actual COLR family");

    let doc = frame.contentDocument;
    is(doc.documentURI, new URL("gdi_synthetic_bold_frame.html", document.documentURI).href,
       "The sample uses a loaded chrome fixture rather than an initial document");
    // Use DOM construction: document.open requires an entry document, which
    // need not exist when this task resumes through the Promise.jsm callback.
    let style = doc.createElement("style");
    style.textContent =
      "@font-face {font-family: LocalChunk; src: local(ChunkFive)}" +
      "@font-face {font-family: DataChunk; src: url('" + fontURI.spec + "')}" +
      "@font-face {font-family: MappedRegular; src: local(Arial); font-weight: 700}" +
      "@font-face {font-family: ActualBold; src: local('Arial Bold'); font-weight: 700}" +
      "@font-face {font-family: LocalColor; src: local(MicroGDIColor)}" +
      "html,body {margin:0; background:white;}" +
      "#text {display:inline-block; margin:12px; font:32px ChunkFive; " +
      "line-height:64px; white-space:pre; color:black;}";
    doc.head.appendChild(style);
    let text = doc.createElement("span");
    text.id = "text";
    text.textContent = SAMPLE;
    doc.body.appendChild(text);
    for (let font of ["32px LocalChunk", "32px DataChunk",
                       "700 32px MappedRegular", "700 32px ActualBold",
                       "32px LocalColor"]) {
      let faces = yield doc.fonts.load(font, SAMPLE);
      is(faces.length, 1, font + " loads the requested face without fallback");
    }
    yield doc.fonts.ready;

    let installedNormal = snapshot(text, FONT_FAMILY, 400);
    let localNormal = snapshot(text, "LocalChunk", 400);
    assertSameFace(installedNormal, FONT_FAMILY, false, "Installed regular");
    assertSameFace(localNormal, FONT_FAMILY, true, "Local regular");
    ok(localNormal.pixels === installedNormal.pixels,
       "A local alias preserves regular glyphs and advances");
    is(localNormal.width, installedNormal.width, "Regular alias metrics agree");

    let installedBold = snapshot(text, FONT_FAMILY, 700);
    let localBold = snapshot(text, "LocalChunk", 700);
    assertSameFace(installedBold, FONT_FAMILY, false, "Installed synthetic bold");
    assertSameFace(localBold, FONT_FAMILY, true, "Local synthetic bold");
    ok(localBold.pixels === installedBold.pixels,
       "Installed and local synthetic bold use identical glyphs and drawing");
    is(localBold.width, installedBold.width, "Synthetic-bold advances agree");
    ok(installedBold.pixels !== installedNormal.pixels,
          "Synthetic bold changes the regular glyphs");
    ok(installedBold.width > installedNormal.width,
       "Proportional synthetic-bold glyphs receive adjusted advances");

    for (let family of [FONT_FAMILY, "LocalChunk"]) {
      let noSynthesis = snapshot(text, family, 700, "none");
      ok(noSynthesis.pixels === installedNormal.pixels,
         family + ": font-synthesis:none preserves regular rendering");
      is(noSynthesis.width, installedNormal.width,
         family + ": font-synthesis:none preserves regular advances");
    }

    // A style descriptor can map a physical regular face to CSS weight 700.
    // Asking GDI for a different weight would violate that mapping (bug 724231).
    let arialNormal = snapshot(text, "Arial", 400);
    let mappedRegular = snapshot(text, "MappedRegular", 700);
    let arialBold = snapshot(text, "Arial", 700);
    let actualBold = snapshot(text, "ActualBold", 700);
    assertSameFace(arialNormal, "Arial", false, "Actual Arial regular");
    assertSameFace(mappedRegular, "Arial", true, "CSS-bold mapped regular");
    assertSameFace(arialBold, "Arial Bold", false, "Actual Arial bold");
    assertSameFace(actualBold, "Arial Bold", true, "CSS-bold actual bold");
    ok(mappedRegular.pixels === arialNormal.pixels,
       "A regular face mapped to CSS bold is not replaced or synthesized");
    ok(actualBold.pixels === arialBold.pixels, "A real bold face is not synthesized again");
    ok(actualBold.pixels !== mappedRegular.pixels,
          "The real bold face differs from the regular face mapped to CSS bold");
    ok(snapshot(text, "Arial", 700, "none").pixels === arialBold.pixels,
       "Disabling synthesis does not remove a real bold face");

    let downloadedNormal = snapshot(text, "DataChunk", 400);
    let downloadedBold = snapshot(text, "DataChunk", 700);
    assertSameFace(downloadedNormal, FONT_FAMILY, true, "Downloaded regular");
    assertSameFace(downloadedBold, FONT_FAMILY, true, "Downloaded synthetic bold");
    ok(downloadedNormal.pixels !== downloadedBold.pixels,
          "Downloaded fonts retain their separate GDI bold synthesis");
    ok(snapshot(text, "DataChunk", 700, "none").pixels === downloadedNormal.pixels,
       "Downloaded font-synthesis:none still suppresses synthetic bold");

    for (let style of ["font-size:16px", "font-size:48px", "zoom:1.5",
                        "transform:scale(1.25); transform-origin:top left",
                        "writing-mode:vertical-rl"]) {
      let installed = snapshot(text, FONT_FAMILY, 700, "weight style", style);
      let local = snapshot(text, "LocalChunk", 700, "weight style", style);
      ok(local.pixels === installed.pixels, "Synthetic bold agrees for " + style);
      is(local.width, installed.width, "Synthetic-bold width agrees for " + style);
      is(local.height, installed.height, "Synthetic-bold height agrees for " + style);
    }

    // A single glyph prevents increased advances from disguising a lost bold
    // effect. COLR drawing bypasses gfxFont's ordinary multi-strike path, so the
    // installed-font path must retain GDI's bold simulation for its layers.
    text.textContent = "A";
    let colorNormal = snapshot(text, "MicroGDIColor", 400);
    let colorBold = snapshot(text, "MicroGDIColor", 700);
    let colorNoSynthesis = snapshot(text, "MicroGDIColor", 700, "none");
    let localColorNormal = snapshot(text, "LocalColor", 400);
    let localColorBold = snapshot(text, "LocalColor", 700);
    assertSameFace(colorNormal, "MicroGDIColor", false, "Installed COLR regular");
    assertSameFace(colorBold, "MicroGDIColor", false, "Installed COLR synthetic bold");
    assertSameFace(localColorNormal, "MicroGDIColor", true, "Local COLR regular");
    assertSameFace(localColorBold, "MicroGDIColor", true, "Local COLR synthetic bold");
    ok(colorNormal.coloredPixels > 0, "The COLR palette is rendered in color");
    ok(colorBold.coloredPixels > 0, "The bold COLR palette stays in color");
    ok(colorBold.pixels !== colorNormal.pixels,
       "Installed COLR bold changes the single glyph's drawing");
    ok(colorNoSynthesis.pixels === colorNormal.pixels,
       "Installed COLR font-synthesis:none preserves regular glyph drawing");
    is(colorNoSynthesis.width, colorNormal.width,
       "Installed COLR font-synthesis:none preserves regular advances");
    for (let [name, result] of [["installed normal", colorNormal],
                               ["installed bold", colorBold],
                               ["installed no synthesis", colorNoSynthesis],
                               ["local normal", localColorNormal],
                               ["local bold", localColorBold]]) {
      logSnapshot("COLR " + name, result);
    }
  } finally {
    // Release text-run and user-font references before removing the resource.
    // Cleanup errors must neither mask the original failure nor prevent the
    // native resource removal or DLL close.
    try {
      frame.remove();
    } catch (error) {
      ok(false, "Could not release the sample frame: " + error);
    }
    try {
      for (let file of registered) {
        ok(!!removeFont(file.path, FR_PRIVATE, null),
           file.leafName + " process-private font registration is removed");
      }
      if (registered.length) {
        fontEnumerator.updateFontList();
      }
    } catch (error) {
      ok(false, "Could not remove the process-private test font: " + error);
    } finally {
      gdi.close();
    }
  }
}

function logSnapshot(description, result) {
  let stream = Cc["@mozilla.org/io/string-input-stream;1"]
                 .createInstance(Ci.nsIStringInputStream);
  stream.setData(result.pixels, result.pixels.length);
  let hash = Cc["@mozilla.org/security/hash;1"].createInstance(Ci.nsICryptoHash);
  hash.init(hash.SHA256);
  hash.updateFromStream(stream, -1);
  info(description + " width=" + result.width + " height=" + result.height +
       " coloredPixels=" + result.coloredPixels + " PNG-SHA256=" + hash.finish(true));
}

function assertSameFace(result, name, userFont, description) {
  info(description + " native faces: " + JSON.stringify(result.faces));
  is(result.faces.length, 1, description + " uses one native face");
  if (result.faces.length !== 1) {
    return;
  }
  is(result.faces[0].name, name, description + " uses the expected physical face");
  is(result.faces[0].userFont, userFont, description + " uses the expected font source");
}

function snapshot(text, family, weight, synthesis = "weight style", extraStyle = "") {
  text.style.cssText = "font-family:'" + family + "';font-weight:" + weight +
                       ";font-synthesis:" + synthesis + ";" + extraStyle;
  let doc = text.ownerDocument;
  let rect = text.getBoundingClientRect();
  ok(rect.left >= 0 && rect.top >= 0 &&
     rect.right <= doc.defaultView.innerWidth &&
     rect.bottom <= doc.defaultView.innerHeight,
     family + " " + weight + " " + extraStyle + " fits completely in the capture");
  let range = doc.createRange();
  range.selectNodeContents(text);
  let nativeFonts = Cc["@mozilla.org/inspector/dom-utils;1"].getService(Ci.inIDOMUtils)
                     .getUsedFontFaces(range);
  let faces = [];
  for (let i = 0; i < nativeFonts.length; i++) {
    let font = nativeFonts.item(i);
    faces.push({name: font.name, userFont: !!font.rule, URI: font.URI,
                localName: font.localName});
  }

  let canvas = doc.createElement("canvas");
  canvas.width = 1000;
  canvas.height = 1000;
  let ctx = canvas.getContext("2d");
  ctx.drawWindow(doc.defaultView, 0, 0, canvas.width, canvas.height, "white");
  let coloredPixels = 0;
  if (faces.some(face => face.name === "MicroGDIColor")) {
    let rgba = ctx.getImageData(0, 0, canvas.width, canvas.height).data;
    for (let i = 0; i < rgba.length; i += 4) {
      if (rgba[i] !== rgba[i + 1] || rgba[i] !== rgba[i + 2]) {
        coloredPixels++;
      }
    }
  }
  return {faces, width: rect.width, height: rect.height,
          coloredPixels, pixels: canvas.toDataURL()};
}
