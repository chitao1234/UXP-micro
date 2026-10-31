/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

// Use the real page-style actor and native font enumeration. A standalone SVG
// or XUL document has no body, including when it is a child of an HTML document.

const FONT_OPTIONS = {
  includePreviews: true,
  previewText: "BODYLESS FONTS"
};

add_task(function* () {
  yield checkBodylessDocument(URL_ROOT + "doc_bodyless-fonts.svg", "SVG");
});

add_task(function* () {
  yield checkBodylessDocument(CHROME_URL_ROOT + "doc_bodyless-fonts.xul", "XUL");
});

add_task(function* () {
  let tab = yield addTab(URL_ROOT + "doc_bodyless-fonts-frame.html");
  let toolbox;
  try {
    yield waitForFixtureFonts(tab.linkedBrowser, false, "BodylessBlack");
    let child = yield waitForFixtureFonts(tab.linkedBrowser, true,
                                          "BodylessRegular");
    ok(!child.hasBody, "The SVG child document has no body");

    let opened = yield openInspector();
    toolbox = opened.toolbox;
    let inspector = opened.inspector;
    let node = yield getNodeFrontInFrame("#font-target", "iframe", inspector);
    let selected = yield inspector.pageStyle.getUsedFontFaces(node, FONT_OPTIONS);
    yield checkFont(selected, "BodylessRegular", "ostrich-regular.ttf",
                    "Selected SVG text in HTML child frame");

    let body = yield getNodeFront("body", inspector);
    let parentFonts = yield inspector.pageStyle.getUsedFontFaces(body, FONT_OPTIONS);
    logFonts(parentFonts, "HTML parent body before SVG text removal");
    is(parentFonts.length, 1, "The HTML parent body uses only its bundled font");
    yield checkFont(parentFonts, "BodylessBlack", "ostrich-black.ttf",
                    "Direct HTML parent body");

    let fonts = yield getAllFonts(inspector, "HTML with a bodyless SVG frame");
    if (!fonts) {
      return;
    }
    is(fonts.length, 2, "Show all includes the HTML and SVG font faces");
    yield checkFont(fonts, "BodylessRegular", "ostrich-regular.ttf", "SVG frame");
    yield checkFont(fonts, "BodylessBlack", "ostrich-black.ttf", "HTML parent");

    yield setFixtureText(tab.linkedBrowser, true, "");
    fonts = yield getAllFonts(inspector, "HTML after emptying the SVG frame");
    if (fonts) {
      is(fonts.length, 1, "Empty SVG does not hide the HTML parent's font");
      yield checkFont(fonts, "BodylessBlack", "ostrich-black.ttf", "HTML parent");
    }
  } finally {
    if (toolbox) {
      yield toolbox.destroy();
    }
    yield removeTab(tab);
  }
});

function* checkBodylessDocument(url, type) {
  let tab = yield addTab(url);
  let toolbox;
  try {
    let documentState = yield waitForFixtureFonts(tab.linkedBrowser, false,
                                                 "BodylessRegular");
    ok(!documentState.hasBody, type + " document has no body");
    is(documentState.loadedFaces, 1, type + " loaded the bundled test font");

    // openFontInspectorForURL selects body unconditionally. Use the inspector
    // directly so this regression reaches the actor with a valid bodyless page.
    let opened = yield openInspector();
    toolbox = opened.toolbox;
    let inspector = opened.inspector;
    let node = yield getNodeFront("#font-target", inspector);
    let selected = yield inspector.pageStyle.getUsedFontFaces(node, FONT_OPTIONS);
    yield checkFont(selected, "BodylessRegular", "ostrich-regular.ttf",
                    type + " selected text");

    let fonts = yield getAllFonts(inspector, type);
    if (!fonts) {
      return;
    }
    is(fonts.length, selected.length,
       type + " show all returns the selected native font");
    yield checkFont(fonts, "BodylessRegular", "ostrich-regular.ttf", type);

    yield setFixtureText(tab.linkedBrowser, false, "");
    fonts = yield getAllFonts(inspector, type + " with no rendered text");
    if (!fonts) {
      return;
    }
    is(fonts.length, 0, type + " with no rendered text returns no fonts");

    yield setFixtureText(tab.linkedBrowser, false, "BODYLESS FONTS");
    fonts = yield getAllFonts(inspector, type + " after restoring its text");
    if (fonts) {
      is(fonts.length, 1, type + " reports the font after live text restoration");
      yield checkFont(fonts, "BodylessRegular", "ostrich-regular.ttf", type);
    }
  } finally {
    if (toolbox) {
      yield toolbox.destroy();
    }
    yield removeTab(tab);
  }
}

function* getAllFonts(inspector, description) {
  try {
    let fonts = yield inspector.pageStyle.getAllUsedFontFaces(FONT_OPTIONS);
    logFonts(fonts, description);
    return fonts;
  } catch (error) {
    ok(false, description + ": getAllUsedFontFaces rejected: " + error);
    // Keep cleanup and the next independent document case reachable on failure.
    return null;
  }
}

function logFonts(fonts, description) {
  info(description + " native font faces: " + JSON.stringify(fonts.map(font => ({
    name: font.name,
    family: font.CSSFamilyName,
    URI: font.URI
  }))));
}

function* checkFont(fonts, family, file, description) {
  let matching = fonts.filter(font => font.CSSFamilyName === family);
  is(matching.length, 1, description + " contains the bundled " + family);
  if (matching.length !== 1) {
    return;
  }
  let font = matching[0];
  is(font.name, family === "BodylessBlack" ? "Ostrich Sans Black" :
                                          "Ostrich Sans Medium",
     description + " returns the native face name");
  ok(font.URI.endsWith("/" + file), description + " returns the bundled font URI");
  ok(font.rule, description + " returns the @font-face rule actor");
  ok(font.preview && font.preview.data, description + " returns a preview actor");
  if (font.preview && font.preview.data) {
    let preview = yield font.preview.data.string();
    ok(preview.startsWith("data:image/png;base64,"),
       description + " preview roundtrips through the long-string actor");
    ok(font.preview.size > 0, description + " preview has nonzero dimensions");
  }
}

function waitForFixtureFonts(browser, inFrame, family) {
  return ContentTask.spawn(browser, {inFrame, family}, function* (args) {
    let doc = args.inFrame ?
      content.document.querySelector("iframe").contentDocument : content.document;
    doc.documentElement.getBoundingClientRect();
    let faces = yield doc.fonts.load("32px " + args.family, "BODYLESS FONTS");
    yield doc.fonts.ready;
    doc.documentElement.getBoundingClientRect();
    return {hasBody: !!doc.body, loadedFaces: faces.length};
  });
}

function setFixtureText(browser, inFrame, text) {
  return ContentTask.spawn(browser, {inFrame, text}, function* (args) {
    let doc = args.inFrame ?
      content.document.querySelector("iframe").contentDocument : content.document;
    doc.getElementById("font-target").textContent = args.text;
    doc.documentElement.getBoundingClientRect();
  });
}
