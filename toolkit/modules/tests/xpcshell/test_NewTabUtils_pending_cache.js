/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

do_get_profile();
const {NewTabUtils} = Components.utils.import("resource://gre/modules/NewTabUtils.jsm", {});

// Adapted from current Firefox's onLinkChangedMidPopulation regression, with
// the provider callback explicitly held until both notification paths run.
add_task(function* onLinkChangedMidPopulation() {
  let requested;
  let queryStarted = new Promise(resolve => requested = resolve);
  let supplyLinks;
  let provider = {
    maxNumLinks: 10,
    addObserver(observer) {
      this.observer = observer;
    },
    getLinks(callback) {
      supplyLinks = callback;
      requested();
    },
    notifyLinkChanged(link) {
      this.observer.onLinkChanged(this, link);
    },
  };
  let snapshot = [{
    url: "http://pending-newtab.example/",
    title: "Provider snapshot",
    frecency: 10,
    lastVisitDate: 1,
    type: "history",
  }];
  let links = NewTabUtils.links;
  links.addProvider(provider);
  do_register_cleanup(() => {
    if (links._providers.has(provider)) {
      links.removeProvider(provider);
    }
  });
  let completed = false;
  let populated = new Promise(resolve => {
    links.populateProviderCache(provider, () => {
      completed = true;
      resolve();
    });
  });

  try {
    yield queryStarted;
    let cache = links._providers.get(provider);
    Assert.ok(cache, "The real population call has published its cache object");
    Assert.strictEqual(cache.linkMap, undefined,
                       "The held provider query has not built the link map");
    Assert.ok(!completed, "The provider query is genuinely still pending");

    provider.notifyLinkChanged({url: snapshot[0].url, title: "Early title"});
    Assert.ok(true, "A title notification during population does not throw");
    provider.notifyLinkChanged({
      url: "http://early-newtab.example/",
      title: "Early full link",
      frecency: 20,
      lastVisitDate: 2,
      type: "history",
    });
    Assert.deepEqual(NewTabUtils.getProviderLinks(provider), [],
                     "Early notifications do not publish a partial cache");

    let callback = supplyLinks;
    supplyLinks = null;
    callback(snapshot);
    yield populated;
    Assert.ok(completed, "The held provider callback completes population");
    Assert.equal(cache.linkMap.size, 1, "The snapshot creates the real link map");
    Assert.deepEqual(NewTabUtils.getProviderLinks(provider), snapshot,
                     "The cache contains the provider's complete snapshot");

    provider.notifyLinkChanged({url: snapshot[0].url, title: "Updated title"});
    Assert.equal(NewTabUtils.getProviderLinks(provider)[0].title, "Updated title",
                 "Notifications update links after population");
    Assert.equal(cache.linkMap.get(snapshot[0].url).title, "Updated title",
                 "The lookup map observes the same positive update");
  } finally {
    if (supplyLinks) {
      let callback = supplyLinks;
      supplyLinks = null;
      callback(snapshot);
    }
    yield populated;
    links.removeProvider(provider);
  }
});
