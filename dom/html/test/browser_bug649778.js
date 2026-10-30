/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// document.open() now replaces the existing history entry instead of creating
// a wyciwyg entry. Generated text from a no-store document must still never
// reach disk or be replayed instead of the original response on reload.
var { LoadContextInfo } = Cu.import("resource://gre/modules/LoadContextInfo.jsm", {});
var { NetUtil } = Cu.import("resource://gre/modules/NetUtil.jsm", {});

function readNoStoreEntry(storage, uri) {
  return new Promise((resolve, reject) => {
    storage.asyncOpenURI(uri, "", Ci.nsICacheStorage.OPEN_READONLY, {
      onCacheEntryCheck() {
        return Ci.nsICacheEntryOpenCallback.ENTRY_WANTED;
      },
      onCacheEntryAvailable(entry, isNew, appCache, status) {
        if (!entry) {
          is(status, Cr.NS_ERROR_CACHE_KEY_NOT_FOUND,
             "A missing no-store entry has the expected status");
          resolve(null);
          return;
        }
        is(status, Cr.NS_OK, "The existing cache entry opens successfully");
        try {
          let persistent = entry.persistent;
          NetUtil.asyncFetch(entry.openInputStream(0), (input, result) => {
            try {
              if (!Components.isSuccessCode(result)) {
                throw new Error("Reading the cache entry failed: " + result);
              }
              let length = input.available();
              let body = length ? NetUtil.readInputStreamToString(input, length) : "";
              resolve({ persistent, body });
            } catch (error) {
              reject(error);
            } finally {
              input.close();
              entry.close();
            }
          });
        } catch (error) {
          entry.close();
          reject(error);
        }
      }
    });
  });
}

function matchingCacheEntries(storage, url) {
  return new Promise(resolve => {
    let entries = [];
    storage.asyncVisitStorage({
      onCacheStorageInfo() {},
      onCacheEntryInfo(uri) {
        // Also catches a legacy wyciwyg://<id>/<original URL> entry without
        // attributing unrelated pages' cache entries to this test.
        if (uri.spec.includes(url)) {
          entries.push(uri);
        }
      },
      onCacheEntryVisitCompleted() {
        resolve(entries);
      }
    }, true);
  });
}

function* checkNoStoreCache(url, sensitiveValues) {
  // Enumerate both the memory table and disk index. Opening through disk
  // storage can also find a memory-only entry, but its index walk does not.
  // Do not clear the cache: that could hide a persistent/generated entry.
  let storage = Services.cache2.diskCacheStorage(LoadContextInfo.default, false);
  let memory = Services.cache2.memoryCacheStorage(LoadContextInfo.default);
  let memoryEntries = yield matchingCacheEntries(memory, url);
  let diskEntries = yield matchingCacheEntries(storage, url);
  let entries = Array.from(new Map(memoryEntries.concat(diskEntries)
    .map(uri => [uri.spec, uri])).values());
  let sourceURI = Services.io.newURI(url, null, null);
  if (!entries.some(uri => uri.spec === url)) {
    // A no-store entry may already have been discarded. Explicitly probe the
    // source URL too, rather than relying solely on the storage index.
    entries.push(sourceURI);
  }
  for (let uri of entries) {
    ok(!uri.schemeIs("wyciwyg"), "document.write creates no generated cache entry");
    let cached = yield readNoStoreEntry(storage, uri);
    if (cached) {
      ok(!cached.persistent, "The no-store response is not persisted to disk");
      for (let value of sensitiveValues) {
        ok(!cached.body.replace(/\0/g, "").includes(value),
           "Generated text is absent from cached response data");
      }
      if (uri.spec === url) {
        ok(cached.body.includes("Original no-store response"),
           "The cached body contains the original server response");
      }
    }
  }
}

add_task(function* () {
  let url = "http://mochi.test:8888/browser/dom/html/test/file_bug649778.html?" +
            Date.now() + "-" + Math.random();
  let tab = yield BrowserTestUtils.openNewForegroundTab(gBrowser, url);
  registerCleanupFunction(() => gBrowser.removeTab(tab));
  let browser = tab.linkedBrowser;
  let history = browser.webNavigation.sessionHistory;
  let historyCount = history.count;
  let sensitiveValues = [];

  for (let pass = 0; pass < 2; ++pass) {
    ok(browser.docShell.currentDocumentChannel.QueryInterface(Ci.nsIHttpChannel)
       .isNoStoreResponse(), "The original response has Cache-Control: no-store");
    let value = "private-document-write-" + pass + "-" + Math.random();
    sensitiveValues.push(value);
    yield ContentTask.spawn(browser, value, function(marker) {
      content.document.getElementById("write").setAttribute("data-marker", marker);
    });
    // document.open checks the entry document's principal. Invoke it from the
    // page's own click handler rather than the privileged ContentTask scope.
    let written = BrowserTestUtils.browserLoaded(browser, false, url);
    yield BrowserTestUtils.synthesizeMouseAtCenter("#write", {}, browser);
    yield written;
    yield ContentTask.spawn(browser, value, function(marker) {
      is(content.document.getElementById("generated").textContent, marker,
         "document.write displays the generated text");
    });
    is(browser.currentURI.spec, url, "document.open preserves the original URL");
    is(history.count, historyCount, "document.open replaces the existing history entry");
    is(history.getEntryAtIndex(history.index, false).URI.spec, url,
       "The current history entry retains the original URL");
    yield* checkNoStoreCache(url, sensitiveValues);

    let loaded = BrowserTestUtils.browserLoaded(browser, false, url);
    browser.reload();
    yield loaded;
    yield ContentTask.spawn(browser, sensitiveValues, function(markers) {
      is(content.document.getElementById("original").textContent, "Original no-store response",
         "Reload restores the server response");
      ok(!content.document.getElementById("generated"), "Reload does not replay generated markup");
      for (let marker of markers) {
        ok(!content.document.documentElement.textContent.includes(marker),
           "Reload does not expose previously generated text");
      }
    });
    is(browser.currentURI.spec, url, "Reload retains the original URL");
    is(history.count, historyCount, "Reload adds no history entry");
    yield* checkNoStoreCache(url, sensitiveValues);
  }
});
