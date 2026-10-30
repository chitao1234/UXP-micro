/*
 * Description of the tests:
 * Tests check that default-src can be overridden by manifest-src.
 */
/*globals Cu, is, ok*/
"use strict";
const {
  ManifestObtainer
} = Cu.import("resource://gre/modules/ManifestObtainer.jsm", {});
const path = "/tests/dom/security/test/csp/";
const testFile = `${path}file_web_manifest.html`;
const mixedContentFile = `${path}file_web_manifest_mixed_content.html`;
const server = `${path}file_testserver.sjs`;
const defaultURL = new URL(`http://example.org${server}`);
const mixedURL = new URL(`http://mochi.test:8888${server}`);
const tests = [
  // Check interaction with default-src and another origin,
  // CSP allows fetching from example.org, so manifest should load.
  {
    expected: `CSP manifest-src overrides default-src of elsewhere.com`,
    get tabURL() {
      const url = new URL(defaultURL);
      url.searchParams.append("file", testFile);
      url.searchParams.append("cors", "*");
      url.searchParams.append("csp", "default-src http://elsewhere.com; manifest-src http://example.org");
      return url.href;
    },
    run(manifest) {
      is(manifest.name, "loaded", this.expected);
    }
  },
  // Check interaction with default-src self,
  // CSP allows fetching manifest from example.org, so manifest should load.
  {
    expected: `CSP manifest-src overrides default-src`,
    get tabURL() {
      const url = new URL(mixedURL);
      url.searchParams.append("file", mixedContentFile);
      url.searchParams.append("csp", "default-src 'self'; manifest-src http://example.org");
      return url.href;
    },
    run(manifest) {
      is(manifest.name, "loaded", this.expected);
    }
  },
];

add_task(function* () {
  for (const test of tests) {
    const tabOptions = {
      gBrowser,
      url: test.tabURL,
      skipAnimation: true,
    };
    yield BrowserTestUtils.withNewTab(tabOptions,
      browser => testObtainingManifest(browser, test));
  }
});

function* testObtainingManifest(aBrowser, aTest) {
  try {
    const manifest = yield ManifestObtainer.browserObtainManifest(aBrowser);
    aTest.run(manifest);
  } catch (e) {
    // Both cases permit this manifest. Treating a NetworkError as success
    // would test neither override and concealed the old wrong-origin fixture.
    ok(false, `Allowed manifest must load from ${aTest.tabURL}: ${e.message}`);
  }
}
