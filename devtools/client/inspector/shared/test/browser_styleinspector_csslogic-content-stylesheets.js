/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Check stylesheets on HTML and XUL documents.

// FIXME: this test opens the devtools for nothing, it should be changed into a
// devtools/server/tests/mochitest/test_css-logic-...something...html
// test

const TEST_URI_HTML = TEST_URL_ROOT + "doc_content_stylesheet.html";
const TEST_URI_AUTHOR = TEST_URL_ROOT + "doc_author-sheet.html";
const TEST_URI_XUL = TEST_URL_ROOT + "doc_content_stylesheet.xul";
const XUL_URI = Cc["@mozilla.org/network/io-service;1"]
                .getService(Ci.nsIIOService)
                .newURI(TEST_URI_XUL, null, null);
var ssm = Components.classes["@mozilla.org/scriptsecuritymanager;1"]
                            .getService(Ci.nsIScriptSecurityManager);
const XUL_PRINCIPAL = ssm.createCodebasePrincipal(XUL_URI, {});

add_task(function* () {
  requestLongerTimeout(2);

  info("Checking stylesheets on HTML document");
  yield addTab(TEST_URI_HTML);

  let {inspector, testActor} = yield openInspector();
  yield selectNode("#target", inspector);

  info("Checking stylesheets");
  yield checkSheets("#target", testActor, TEST_URI_HTML,
    /doc_content_stylesheet_linked\.css$/);

  info("Checking authored stylesheets");
  yield addTab(TEST_URI_AUTHOR);

  ({inspector, testActor} = yield openInspector());
  yield selectNode("#target", inspector);
  yield checkSheets("#target", testActor, TEST_URI_AUTHOR, /^data:.*seagreen/);

  info("Checking stylesheets on XUL document");
  info("Allowing XUL content");
  registerCleanupFunction(disallowXUL);
  allowXUL();
  yield addTab(TEST_URI_XUL);

  ({inspector, testActor} = yield openInspector());
  yield selectNode("#target", inspector);

  yield checkSheets("#target", testActor, TEST_URI_XUL,
    /doc_content_stylesheet_xul\.css$/);
  info("Disallowing XUL content");
  disallowXUL();
});

function allowXUL() {
  Cc["@mozilla.org/permissionmanager;1"].getService(Ci.nsIPermissionManager)
    .addFromPrincipal(XUL_PRINCIPAL, "allowXULXBL",
      Ci.nsIPermissionManager.ALLOW_ACTION);
}

function disallowXUL() {
  Cc["@mozilla.org/permissionmanager;1"].getService(Ci.nsIPermissionManager)
    .addFromPrincipal(XUL_PRINCIPAL, "allowXULXBL",
      Ci.nsIPermissionManager.DENY_ACTION);
}

function* checkSheets(targetSelector, testActor, expectedURI, expectedSheet) {
  // Each openInspector creates a document-specific test actor. Keeping an old
  // actor here silently re-tests the first tab for the authored and XUL cases.
  is(yield testActor.eval("content.document.documentURI"), expectedURI,
    "Stylesheet inspection uses the current document");
  let sheets = yield testActor.getStyleSheetsInfoForNode(targetSelector);
  ok(sheets.length > 0, "The selected node has matching stylesheets");
  ok(sheets.some(sheet => expectedSheet.test(sheet.href)),
    "The document-specific stylesheet is present");

  for (let sheet of sheets) {
    if (!sheet.href ||
        /doc_content_stylesheet_/.test(sheet.href) ||
        // For the "authored" case.
        /^data:.*seagreen/.test(sheet.href)) {
      ok(sheet.isContentSheet,
        sheet.href + " identified as content stylesheet");
    } else {
      ok(!sheet.isContentSheet,
        sheet.href + " identified as non-content stylesheet");
    }
  }
}
