
"use strict";

const Ci = Components.interfaces;
const IMAGE_TEST_PAGE = TESTROOT + "file_docshell_type_editor.html";
const PRIVILEGED_IMAGE = "chrome://devtools/content/debugger/new/images/angle-brackets.svg";

// The following URI is *not* accessible to content, hence loading that URI
// from an unprivileged site should be blocked. If docshell is of appType
// APP_TYPE_EDITOR however the load should be allowed.
// Use an existing resource in the non-content-accessible DevTools package.

add_task(function* () {
  let image = new Image();
  yield new Promise((resolve, reject) => {
    image.onload = resolve;
    image.onerror = () => reject(new Error("The privileged image fixture is missing"));
    image.src = PRIVILEGED_IMAGE;
  });
  ok(image.naturalWidth > 0, "The privileged fixture loads in chrome");
});

function* checkPrivilegedImage(appType, shouldLoad) {
  yield BrowserTestUtils.withNewTab({
    gBrowser,
    url: IMAGE_TEST_PAGE
  }, function* (browser) {
    let defaultAppType = null;
    try {
      let result = BrowserTestUtils.waitForContentEvent(browser,
        "Test:PrivilegedImageResult", true, null, true);
      // This second task is also a message-ordering barrier: the preceding
      // per-browser event listener is installed before we synthesize a click.
      defaultAppType = yield ContentTask.spawn(browser,
        {appType, imageURI: PRIVILEGED_IMAGE}, function(options) {
          let rootDocShell = docShell.QueryInterface(Ci.nsIDocShellTreeItem)
                                    .rootTreeItem.QueryInterface(Ci.nsIDocShell);
          content.document.getElementById("load-image")
            .setAttribute("data-image-url", options.imageURI);
          let previous = rootDocShell.appType;
          rootDocShell.appType = options.appType;
          is(rootDocShell.appType, options.appType, "The requested appType is active");
          return previous;
        });

      // Setting img.src in ContentTask would use its system triggering
      // principal and make the negative case pass the security check. The
      // fixture's own handler must initiate the image request.
      yield BrowserTestUtils.synthesizeMouseAtCenter("#load-image", {}, browser);
      yield result;
      let actual = yield ContentTask.spawn(browser, null, function() {
        return {
          status: content.document.documentElement.getAttribute("data-image-result"),
          width: content.document.getElementById("privileged-image").naturalWidth
        };
      });
      is(actual.status, shouldLoad ? "load" : "error",
         shouldLoad ? "APP_TYPE_EDITOR is allowed to load the privileged image" :
                      "APP_TYPE_UNKNOWN cannot load the privileged image");
      is(actual.width > 0, shouldLoad, "Decoded image state matches the access policy");
    } finally {
      if (defaultAppType !== null) {
        yield ContentTask.spawn(browser, defaultAppType, function(previous) {
          let rootDocShell = docShell.QueryInterface(Ci.nsIDocShellTreeItem)
                                    .rootTreeItem.QueryInterface(Ci.nsIDocShell);
          rootDocShell.appType = previous;
          is(rootDocShell.appType, previous, "The original appType is restored");
        });
      }
    }
  });
}

add_task(function* () {
  yield* checkPrivilegedImage(Ci.nsIDocShell.APP_TYPE_EDITOR, true);
});

add_task(function* () {
  yield* checkPrivilegedImage(Ci.nsIDocShell.APP_TYPE_UNKNOWN, false);
});
