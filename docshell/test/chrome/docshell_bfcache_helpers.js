/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Like current Firefox's docshell helper, ignore only automatically created
// initial documents, not explicit navigations to the same about:blank URI.
function isInitialDocumentEvent(event) {
  const document = event.originalTarget;
  return document instanceof Document && document.isInitialDocument;
}

// pageshow and readyState=complete can precede scrollbar/theme image requests.
// BFCache rejects any pending document request, including background requests
// which nsILoadGroup deliberately omits from web-progress notifications.
function waitForBFCacheReady(browser) {
  const Ci = Components.interfaces;
  const progress = browser.docShell.QueryInterface(Ci.nsIWebProgress);
  let pending = [];

  function collect(shell) {
    const win = shell.QueryInterface(Ci.nsIInterfaceRequestor)
                     .getInterface(Ci.nsIDOMWindow);
    if (win.document.readyState !== "complete" ||
        shell.QueryInterface(Ci.nsIWebProgress).isLoadingDocument) {
      pending.push("loading " + win.document.documentURI);
    }
    const requests = shell.QueryInterface(Ci.nsIDocumentLoader).loadGroup.requests;
    while (requests.hasMoreElements()) {
      pending.push(requests.getNext().QueryInterface(Ci.nsIRequest).name);
    }
    const children = shell.QueryInterface(Ci.nsIDocShellTreeItem);
    for (let i = 0; i < children.childCount; i++) {
      collect(children.getChildAt(i));
    }
  }

  return new Promise((resolve, reject) => {
    let frame;
    let paints = 0;
    let settled = false;
    const timer = setTimeout(() => {
      settle(new Error("BFCache readiness timed out: " + pending.join(", ")));
    }, 30000);
    const listener = {
      onStateChange() { schedule(); },
      QueryInterface(iid) {
        if (iid.equals(Ci.nsIWebProgressListener) ||
            iid.equals(Ci.nsISupportsWeakReference) || iid.equals(Ci.nsISupports)) {
          return this;
        }
        throw Components.results.NS_ERROR_NO_INTERFACE;
      }
    };

    function settle(error) {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      cancelAnimationFrame(frame);
      progress.removeProgressListener(listener);
      window.removeEventListener("unload", onUnload);
      if (error) reject(error);
      else resolve();
    }
    function onUnload() {
      settle(new Error("Window closed while waiting for BFCache readiness"));
    }
    function schedule() {
      if (!settled && frame === undefined) {
        frame = requestAnimationFrame(check);
      }
    }
    function check() {
      frame = undefined;
      if (settled) return;
      try {
        // Match the two-paint boundary used by newer docshell test helpers.
        // Layout can create native scrollbar requests after pageshow.
        browser.contentDocument.documentElement.getBoundingClientRect();
        pending = [];
        collect(browser.docShell);
        if (++paints >= 2 && pending.length === 0) {
          settle();
          return;
        }
      } catch (error) {
        settle(error);
        return;
      }
      schedule();
    }
    progress.addProgressListener(listener, Ci.nsIWebProgress.NOTIFY_STATE_ALL);
    window.addEventListener("unload", onUnload);
    schedule();
  });
}
