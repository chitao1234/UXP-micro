/*
 * Description of the tests:
 *   These tests check for conformance to the CSP spec as they relate to Web Manifests.
 *
 *   In particular, the tests check that default-src and manifest-src directives are
 *   are respected by the ManifestObtainer.
 */
/*globals Cu, is, ok*/
"use strict";
const {
  ManifestObtainer
} = Cu.import("resource://gre/modules/ManifestObtainer.jsm", {});
const path = "/tests/dom/security/test/csp/";
const testFile = `${path}file_web_manifest.html`;
const remoteFile = `${path}file_web_manifest_remote.html`;
const httpsManifest = `${path}file_web_manifest_https.html`;
const server = `${path}file_testserver.sjs`;
const defaultURL = new URL(`http://example.org${server}`);
const secureURL = new URL(`https://example.com:443${server}`);
const tests = [
  // CSP block everything, so trying to load a manifest
  // will result in a policy violation.
  {
    expected: "default-src 'none' blocks fetching manifest.",
    blocked: true,
    get tabURL() {
      const url = new URL(defaultURL);
      url.searchParams.append("file", testFile);
      url.searchParams.append("csp", "default-src 'none'");
      return url.href;
    },
    run(topic) {
      is(topic, "securitypolicyviolation", this.expected);
    }
  },
  // CSP allows fetching only from mochi.test:8888,
  // so trying to load a manifest from same origin
  // triggers a CSP violation.
  {
    expected: "default-src mochi.test:8888 blocks manifest fetching.",
    blocked: true,
    get tabURL() {
      const url = new URL(defaultURL);
      url.searchParams.append("file", testFile);
      url.searchParams.append("csp", "default-src mochi.test:8888");
      return url.href;
    },
    run(topic) {
      is(topic, "securitypolicyviolation", this.expected);
    }
  },
  // CSP restricts fetching to 'self', so allowing the manifest
  // to load. The name of the manifest is then checked.
  {
    expected: "CSP default-src 'self' allows fetch of manifest.",
    get tabURL() {
      const url = new URL(defaultURL);
      url.searchParams.append("file", testFile);
      url.searchParams.append("csp", "default-src 'self'");
      return url.href;
    },
    run(manifest) {
      is(manifest.name, "loaded", this.expected);
    }
  },
  // CSP only allows fetching from mochi.test:8888 and remoteFile
  // requests a manifest from that origin, so manifest should load.
  {
    expected: "CSP default-src mochi.test:8888 allows fetching manifest.",
    get tabURL() {
      const url = new URL(defaultURL);
      url.searchParams.append("file", remoteFile);
      url.searchParams.append("csp", "default-src http://mochi.test:8888");
      return url.href;
    },
    run(manifest) {
      is(manifest.name, "loaded", this.expected);
    }
  },
  // default-src blocks everything, so any attempt to
  // fetch a manifest from another origin will trigger a
  // policy violation.
  {
    expected: "default-src 'none' blocks mochi.test:8888",
    blocked: true,
    get tabURL() {
      const url = new URL(defaultURL);
      url.searchParams.append("file", remoteFile);
      url.searchParams.append("csp", "default-src 'none'");
      return url.href;
    },
    run(topic) {
      is(topic, "securitypolicyviolation", this.expected);
    }
  },
  // CSP allows fetching from self, so manifest should load.
  {
    expected: "CSP manifest-src allows self",
    get tabURL() {
      const url = new URL(defaultURL);
      url.searchParams.append("file", testFile);
      url.searchParams.append("csp", "manifest-src 'self'");
      return url.href;
    },
    run(manifest) {
      is(manifest.name, "loaded", this.expected);
    }
  },
  // CSP allows fetching from example.org, so manifest should load.
  {
    expected: "CSP manifest-src allows http://example.org",
    get tabURL() {
      const url = new URL(defaultURL);
      url.searchParams.append("file", testFile);
      url.searchParams.append("csp", "manifest-src http://example.org");
      return url.href;
    },
    run(manifest) {
      is(manifest.name, "loaded", this.expected);
    }
  }, {
    expected: "CSP manifest-src allows mochi.test:8888",
    get tabURL() {
      const url = new URL(defaultURL);
      url.searchParams.append("file", remoteFile);
      url.searchParams.append("cors", "*");
      url.searchParams.append("csp", "default-src *; manifest-src http://mochi.test:8888");
      return url.href;
    },
    run(manifest) {
      is(manifest.name, "loaded", this.expected);
    }
  },
  // CSP restricts fetching to mochi.test:8888, but the test
  // file is at example.org. Hence, a policy violation is
  // triggered.
  {
    expected: "CSP blocks manifest fetching from example.org.",
    blocked: true,
    get tabURL() {
      const url = new URL(defaultURL);
      url.searchParams.append("file", testFile);
      url.searchParams.append("csp", "manifest-src mochi.test:8888");
      return url.href;
    },
    run(topic) {
      is(topic, "securitypolicyviolation", this.expected);
    }
  },
  // CSP is set to only allow manifest to be loaded from same origin,
  // but the remote file attempts to load from a different origin. Thus
  // this causes a CSP violation.
  {
    expected: "CSP manifest-src 'self' blocks cross-origin fetch.",
    blocked: true,
    get tabURL() {
      const url = new URL(defaultURL);
      url.searchParams.append("file", remoteFile);
      url.searchParams.append("csp", "manifest-src 'self'");
      return url.href;
    },
    run(topic) {
      is(topic, "securitypolicyviolation", this.expected);
    }
  },
  // CSP allows fetching over TLS from example.com, so manifest should load.
  {
    expected: "CSP manifest-src allows example.com over TLS",
    get tabURL() {
      // secureURL loads https://example.com:443
      // and gets manifest from https://example.com:443
      const url = new URL(secureURL);
      url.searchParams.append("file", httpsManifest);
      url.searchParams.append("cors", "*");
      url.searchParams.append("csp", "manifest-src https://example.com:443");
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
  // Current Firefox observes this tab's DOM event rather than a process-wide
  // notification which another test's violation could satisfy.
  const violation = aTest.blocked ? BrowserTestUtils.waitForContentEvent(
    aBrowser, "securitypolicyviolation", true, event =>
      event.effectiveDirective === "manifest-src" ||
      event.effectiveDirective === "default-src") : null;
  if (violation) {
    // The event listener is installed by a content task. A following task on
    // the same message manager provides an installation barrier before fetch.
    yield ContentTask.spawn(aBrowser, null, () => {});
  }

  let manifest;
  try {
    manifest = yield ManifestObtainer.browserObtainManifest(aBrowser);
  } catch (e) {
    if (!aTest.blocked) {
      ok(false, `Allowed manifest must load from ${aTest.tabURL}: ${e.message}`);
      return;
    }
    const wasBlocked = e.message.includes("NetworkError when attempting to fetch resource");
    ok(wasBlocked, `Expected promise rejection obtaining ${aTest.tabURL}: ${e.message}`);
    if (aTest.blocked && wasBlocked) {
      yield violation;
      aTest.run("securitypolicyviolation");
    }
    return;
  }

  if (aTest.blocked) {
    ok(false, `Blocked manifest must not load from ${aTest.tabURL}`);
    return;
  }
  aTest.run(manifest);
}
