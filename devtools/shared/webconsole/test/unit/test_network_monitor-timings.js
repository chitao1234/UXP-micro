/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { require } = Components.utils.import("resource://devtools/shared/Loader.jsm", {});
const { NetworkMonitor } = require("devtools/shared/webconsole/network-monitor");
const Ci = Components.interfaces;
// Use actual epoch-scale microseconds: subtracting a missing native mark must
// not turn the date into a multi-trillion-millisecond request duration.
const EPOCH = 1793420000000000;

function measure(channelTimes = {}, activityTimes = {}, fromCache = false) {
  let channel = Object.assign({
    asyncOpenTime: EPOCH,
    connectStartTime: 0,
    connectEndTime: 0,
    secureConnectionStartTime: 0,
    domainLookupStartTime: 0,
    domainLookupEndTime: 0,
    QueryInterface(iid) {
      ok(iid.equals(Ci.nsITimedChannel), "The channel timing interface is requested.");
      return this;
    }
  }, channelTimes);
  let timings = Object.assign({
    REQUEST_HEADER: { first: EPOCH, last: EPOCH },
    STATUS_SENDING_TO: { first: EPOCH + 1000, last: EPOCH + 3000 },
    REQUEST_BODY_SENT: { first: EPOCH + 3000, last: EPOCH + 3000 },
    RESPONSE_START: { first: EPOCH + 13000, last: EPOCH + 13000 },
    RESPONSE_COMPLETE: { first: EPOCH + 17000, last: EPOCH + 17000 }
  }, activityTimes);
  let result = NetworkMonitor.prototype._setupHarTimings({ channel, timings }, fromCache);
  let total = 0;
  for (let name of Object.keys(result.timings)) {
    let duration = result.timings[name];
    ok(Number.isInteger(duration) && duration >= -1,
       name + " is an integer duration or the unavailable sentinel.");
    total += Math.max(duration, 0);
  }
  equal(result.total, total, "The total sums only available phase durations.");
  return result;
}

function run_test() {
  let result = measure();
  equal(result.timings.dns, -1, "An absent DNS interval stays unavailable.");
  equal(result.timings.connect, -1, "An absent connection interval stays unavailable.");
  equal(result.timings.ssl, -1, "An absent TLS interval stays unavailable.");
  equal(result.total, 17, "Activity-only request duration is preserved.");

  result = measure({
    connectEndTime: EPOCH + 15000,
    secureConnectionStartTime: EPOCH + 12000,
    domainLookupEndTime: EPOCH + 3000
  });
  equal(result.timings.connect, -1, "A missing connect start is not epoch zero.");
  equal(result.timings.ssl, -1, "Partial connection marks cannot invent a TLS interval.");
  equal(result.timings.dns, -1, "A missing DNS start is not epoch zero.");
  equal(result.total, 17, "Missing starts do not add the current date to the total.");

  result = measure({
    connectStartTime: EPOCH + 10000,
    domainLookupStartTime: EPOCH + 1000
  });
  equal(result.timings.connect, -1, "A missing connect end stays unavailable.");
  equal(result.timings.dns, -1, "A missing DNS end stays unavailable.");
  equal(result.total, 17, "Missing ends leave the measured activity duration intact.");

  result = measure({
    connectStartTime: EPOCH + 10000,
    secureConnectionStartTime: EPOCH + 12000,
    connectEndTime: EPOCH + 15000,
    domainLookupStartTime: EPOCH + 1000,
    domainLookupEndTime: EPOCH + 3000
  });
  equal(result.timings.connect, 2, "A complete TCP-before-TLS interval is used.");
  equal(result.timings.ssl, 3, "A complete TLS interval is used.");
  equal(result.timings.dns, 2, "A complete DNS interval is used.");
  equal(result.total, 24, "Complete fallback intervals contribute their durations.");

  result = measure({
    connectStartTime: EPOCH + 10000,
    connectEndTime: EPOCH + 15000
  });
  equal(result.timings.connect, 5, "A connection without TLS uses its full interval.");
  equal(result.timings.ssl, -1, "An absent secure start is not a TLS handshake.");

  result = measure({
    connectStartTime: EPOCH + 10000,
    secureConnectionStartTime: EPOCH + 10000,
    connectEndTime: EPOCH + 10000,
    domainLookupStartTime: EPOCH + 1000,
    domainLookupEndTime: EPOCH + 1000
  });
  equal(result.timings.connect, 0, "Equal positive connect marks measure zero duration.");
  equal(result.timings.ssl, 0, "Equal positive TLS marks measure zero duration.");
  equal(result.timings.dns, 0, "Equal positive DNS marks measure zero duration.");

  result = measure({
    connectStartTime: EPOCH + 15000,
    connectEndTime: EPOCH + 10000,
    domainLookupStartTime: EPOCH + 3000,
    domainLookupEndTime: EPOCH + 1000
  });
  equal(result.timings.connect, -1, "Reversed connect marks do not form a valid interval.");
  equal(result.timings.dns, -1, "Reversed DNS marks do not form a valid interval.");

  result = measure({
    connectStartTime: EPOCH - 3000,
    connectEndTime: EPOCH + 1000,
    secureConnectionStartTime: EPOCH - 1000,
    domainLookupStartTime: EPOCH - 2000,
    domainLookupEndTime: EPOCH + 1000
  });
  equal(result.timings.connect, -1, "A stale speculative connect start is not this request's.");
  equal(result.timings.dns, -1, "A stale speculative DNS start is not this request's.");

  result = measure({}, {}, true);
  for (let name of Object.keys(result.timings)) {
    equal(result.timings[name], 0, "Cached " + name + " stays zero by the cache protocol.");
  }
  equal(result.total, 0, "A cached response keeps the existing zero total.");
}
