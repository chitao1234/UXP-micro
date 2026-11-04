/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const SERVER_MIME_ROOT =
  "http://example.com/browser/testing/mochitest/tests/browser/";

add_task(function* () {
  const ci = Components.interfaces;
  const prefs = Components.classes["@mozilla.org/preferences-service;1"]
                          .getService(ci.nsIPrefBranch);
  const browserPID = Components.classes["@mozilla.org/xre/app-info;1"]
                               .getService(ci.nsIXULRuntime).processID;
  function browserPluginPref() {
    let type = prefs.getPrefType("plugin.disable");
    return {type, user: prefs.prefHasUserValue("plugin.disable"),
            value: type == prefs.PREF_BOOL ? prefs.getBoolPref("plugin.disable") : null};
  }
  const originalBrowserPref = JSON.stringify(browserPluginPref());
  ok(!prefs.getBoolPref("plugin.disable", false),
     "Importing HTTP server template helpers must not disable browser plugins");
  const pluginHost = Components.classes["@mozilla.org/plugin/host;1"]
                               .getService(ci.nsIPluginHost);
  ok(pluginHost.getPluginTags().some(tag => tag.name == "Test Plug-in"),
     "The browser can enumerate the staged test plugin before server requests");
  info("PROFILELESS-MIME-BROWSER " + JSON.stringify({
    processID: browserPID, pluginPref: browserPluginPref()
  }));

  function* serverState(phase) {
    let response = yield fetch(SERVER_MIME_ROOT +
      "file_server_profileless_state.sjs?" + phase, {cache: "no-store"});
    is(response.status, 200, "The server state endpoint responds for " + phase);
    let state = yield response.json();
    info("PROFILELESS-MIME-STATE " + phase + " " + JSON.stringify(state));
    isnot(state.processID, browserPID, "State comes from the HTTP server process");
    ok(!state.hasProfile, "The server has no ProfD for " + phase);
    ok(state.pluginDisabled, "Plugin scanning is disabled only in the server");
    ok(!state.blocklistInstantiated, "Serving fixtures does not construct the blocklist");
    // The native MIME service can construct nsPluginHost directly, outside
    // the service manager. Record its service state, without treating it as
    // proof that no native singleton exists.
    is(typeof state.pluginHostServiceInstantiated, "boolean",
       "The non-constructing plugin-host service query returns a boolean");
    return state;
  }

  let before = yield* serverState("before");
  let unknown = yield fetch(SERVER_MIME_ROOT +
    "file_server_profileless_unknown.5e0a4b7521d74fefbbddfb4ca7b6ea96",
    {cache: "no-store"});
  is(unknown.status, 200, "A genuine unknown-extension file is served");
  is(unknown.headers.get("Content-Type"), "application/octet-stream",
     "The unchanged HTTPD native-MIME fallback produces octet-stream");
  is(yield unknown.text(), "profileless unknown MIME fixture\n",
     "The fallback preserves the fixture bytes");
  let afterUnknown = yield* serverState("after-unknown");

  let overridden = yield fetch(SERVER_MIME_ROOT +
    "file_server_profileless_override.dat", {cache: "no-store"});
  is(overridden.status, 200, "The MIME-override fixture is served");
  is(overridden.headers.get("Content-Type"), "application/x-mochitest-server-override",
     "Explicit fixture headers override the server's registered dat MIME type");
  is(yield overridden.text(), "profileless explicit MIME fixture\n",
     "The explicit override preserves the fixture bytes");
  let afterOverride = yield* serverState("after-override");

  is(afterUnknown.processID, before.processID, "The same server handled the fallback");
  is(afterOverride.processID, before.processID, "The same server handled the override");
  is(JSON.stringify(browserPluginPref()), originalBrowserPref,
     "Server-only policy leaves browser plugin defaults and user prefs unchanged");
});
