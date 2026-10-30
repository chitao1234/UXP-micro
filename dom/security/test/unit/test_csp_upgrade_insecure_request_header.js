var Cu = Components.utils;
var Ci = Components.interfaces;
var Cc = Components.classes;

Cu.import("resource://testing-common/httpd.js");
Cu.import("resource://gre/modules/NetUtil.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

var prefs = Cc["@mozilla.org/preferences-service;1"].
              getService(Ci.nsIPrefBranch);

// Since this test creates a TYPE_DOCUMENT channel via javascript, it will
// end up using the wrong LoadInfo constructor. Setting this pref will disable
// the ContentPolicyType assertion in the constructor.
const SKIP_TYPE_PREF = "network.loadinfo.skip_type_assertion";
const hadSkipTypePref = prefs.prefHasUserValue(SKIP_TYPE_PREF);
const oldSkipTypePref = hadSkipTypePref ? prefs.getBoolPref(SKIP_TYPE_PREF) : false;
prefs.setBoolPref(SKIP_TYPE_PREF, true);

const UPGRADE_PREF = "network.http.upgrade-insecure-requests";
const hadUpgradePref = prefs.prefHasUserValue(UPGRADE_PREF);
const oldUpgradePref = prefs.getBoolPref(UPGRADE_PREF);
do_register_cleanup(() => {
  if (hadUpgradePref) {
    prefs.setBoolPref(UPGRADE_PREF, oldUpgradePref);
  } else {
    prefs.clearUserPref(UPGRADE_PREF);
  }
  if (hadSkipTypePref) {
    prefs.setBoolPref(SKIP_TYPE_PREF, oldSkipTypePref);
  } else {
    prefs.clearUserPref(SKIP_TYPE_PREF);
  }
});

XPCOMUtils.defineLazyGetter(this, "URL", function() {
  return "http://localhost:" + httpserver.identity.primaryPort;
});

var httpserver =  null;
var channel = null;
var curTest = null;
var testpath = "/footpath";

var contentTypes = [
  {
    name: "TYPE_OTHER",
    contentType: Ci.nsIContentPolicy.TYPE_OTHER,
    navigational: false
  },
  {
    name: "TYPE_DOCUMENT",
    contentType: Ci.nsIContentPolicy.TYPE_DOCUMENT,
    navigational: true
  },
  {
    name: "TYPE_SUBDOCUMENT",
    contentType: Ci.nsIContentPolicy.TYPE_SUBDOCUMENT,
    navigational: true
  },
  {
    name: "TYPE_IMAGE",
    contentType: Ci.nsIContentPolicy.TYPE_IMAGE,
    navigational: false
  },
];

var tests = [];
for (let enabled of [true, false]) {
  for (let type of contentTypes) {
    tests.push({
      description: type.name + " with upgrade pref " + enabled,
      contentType: type.contentType,
      enabled,
      expectingHeader: enabled && type.navigational,
    });
  }
}

function ChannelListener() {
}

ChannelListener.prototype = {
  onStartRequest: function(request, context) { },
  onDataAvailable: function(request, context, stream, offset, count) {
    do_throw("Should not get any data!");
  },
  onStopRequest: function(request, context, status) {
    ok(Components.isSuccessCode(status), curTest.description + " load succeeds");
    run_next_test();
  },
};

function setupChannel(aContentType) {
  var chan = NetUtil.newChannel({
    uri: URL + testpath,
    loadUsingSystemPrincipal: true,
    contentPolicyType: aContentType
  });
  chan.QueryInterface(Ci.nsIHttpChannel);
  chan.requestMethod = "GET";
  return chan;
}

function serverHandler(metadata, response) {
  // Check what was actually sent, as well as the channel's request headers.
  const present = metadata.hasHeader("Upgrade-Insecure-Requests");
  equal(present, curTest.expectingHeader, curTest.description);
  if (present) {
    equal(metadata.getHeader("Upgrade-Insecure-Requests"), "1",
          curTest.description + " sends the expected preference token");
    equal(channel.getRequestHeader("Upgrade-Insecure-Requests"), "1",
          curTest.description + " exposes the sent header on the channel");
  }
}

function run_next_test() {
  curTest = tests.shift();
  if (!curTest) {
    httpserver.stop(do_test_finished);
    return;
  }
  // This pref is captured when the channel is constructed.
  prefs.setBoolPref(UPGRADE_PREF, curTest.enabled);
  channel = setupChannel(curTest.contentType);
  channel.asyncOpen2(new ChannelListener());
}

function run_test() {
  // set up the test environment
  httpserver = new HttpServer();
  httpserver.registerPathHandler(testpath, serverHandler);
  httpserver.start(-1);

  do_test_pending();
  run_next_test();
}
