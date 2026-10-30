"use strict";

const kDataBody = "toplevel navigation to data: URI allowed";
const kDataURI = "data:text/html,<body>" + kDataBody + "</body>";
const kTestPath = getRootDirectory(gTestPath)
                  .replace("chrome://mochitests/content", "http://example.com");
const kRedirectURI = kTestPath + "file_toplevel_data_navigations.sjs";
const kMetaRedirectURI = kTestPath + "file_toplevel_data_meta_redirect.html";

// nsContentSecurityManager emits this category only after deciding to block a
// top-level data: navigation. Wait for that decision, not an arbitrary delay or
// the contents of an unrelated selected tab. Match the URL fragment too: the
// server and meta redirects must each produce their own blocked navigation.
function blockedDataNavigation(fragment) {
  let listener;
  function removeListener() {
    if (listener) {
      Services.console.unregisterListener(listener);
      listener = null;
    }
  }
  const blocked = new Promise(resolve => {
    listener = {
      observe(message) {
        let error;
        try {
          error = message.QueryInterface(Ci.nsIScriptError);
        } catch (e) {
          return;
        }
        if (error.category === "DATA_URI_BLOCKED" &&
            error.errorMessage.includes(fragment)) {
          removeListener();
          resolve();
        }
      }
    };
    Services.console.registerListener(listener);
  });
  registerCleanupFunction(removeListener);
  return blocked;
}

add_task(async function test_nav_data_uri() {
  await SpecialPowers.pushPrefEnv({
    "set": [["security.data_uri.block_toplevel_data_uri_navigations", true]],
  });
  await BrowserTestUtils.withNewTab(kDataURI, async function(browser) {
    await ContentTask.spawn(browser, {kDataBody}, async function({kDataBody}) { // eslint-disable-line
     is(content.document.body.innerHTML, kDataBody,
        "data: URI navigation from system should be allowed");
    });
  });
});

add_task(async function test_nav_data_uri_redirect() {
  await SpecialPowers.pushPrefEnv({
    "set": [["security.data_uri.block_toplevel_data_uri_navigations", true]],
  });
  await BrowserTestUtils.withNewTab("about:blank", async function(browser) {
    const blocked = blockedDataNavigation("toplevel data: URI");
    const stopped = BrowserTestUtils.browserStopped(browser);
    browser.loadURI(kRedirectURI);
    await Promise.all([blocked, stopped]);
    ok(true, "Server redirect attempted a top-level data: navigation and was blocked");
    await ContentTask.spawn(browser, {}, async function() {
      is(content.document.body.innerHTML, "",
         "Blocked server redirect preserves the initial blank document");
      is(content.document.documentURI, "about:blank",
         "The redirect's data: document was not committed");
    });
  });
});

add_task(async function test_nav_data_uri_meta_redirect() {
  await SpecialPowers.pushPrefEnv({
    "set": [["security.data_uri.block_toplevel_data_uri_navigations", true]],
  });
  await BrowserTestUtils.withNewTab("about:blank", async function(browser) {
    const blocked = blockedDataNavigation("toplevel meta redirect");
    const loaded = BrowserTestUtils.browserLoaded(browser, false, kMetaRedirectURI);
    browser.loadURI(kMetaRedirectURI);
    await Promise.all([loaded, blocked]);
    ok(true, "Meta refresh attempted a top-level data: navigation and was blocked");
    await ContentTask.spawn(browser, {kMetaRedirectURI}, async function({kMetaRedirectURI}) {
      is(content.document.body.textContent.trim(), "Meta Redirect to data: URI",
         "Blocked meta refresh preserves the original page, not the data: payload");
      is(content.document.documentURI, kMetaRedirectURI,
         "The meta refresh's data: document was not committed");
    });
  });
});
