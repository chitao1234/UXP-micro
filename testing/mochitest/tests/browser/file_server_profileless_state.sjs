/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

function handleRequest(request, response) {
  var ci = Components.interfaces;
  var services = Components.manager.QueryInterface(ci.nsIServiceManager);
  // ErrorList.h: NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_XPCOM, 22).
  // This internal result is not exported through Components.results in 52.
  var NS_ERROR_SERVICE_NOT_AVAILABLE = 0x80460016;
  function instantiated(contract) {
    try {
      return services.isServiceInstantiatedByContractID(contract, ci.nsISupports);
    } catch (error) {
      // This API reports absence with this nsresult instead of false. Do not
      // construct a service just to inspect it, or hide any other error.
      if (error.result == NS_ERROR_SERVICE_NOT_AVAILABLE) {
        return false;
      }
      throw error;
    }
  }

  var prefs = Components.classes["@mozilla.org/preferences-service;1"]
                        .getService(ci.nsIPrefBranch);
  var directory = Components.classes["@mozilla.org/file/directory_service;1"]
                            .getService(ci.nsIProperties);
  var runtime = Components.classes["@mozilla.org/xre/app-info;1"]
                          .getService(ci.nsIXULRuntime);
  response.setHeader("Content-Type", "application/json", false);
  response.setHeader("Cache-Control", "no-store", false);
  response.write(JSON.stringify({
    processID: runtime.processID,
    hasProfile: directory.has("ProfD"),
    pluginDisabled: prefs.getPrefType("plugin.disable") == prefs.PREF_BOOL &&
                    prefs.getBoolPref("plugin.disable"),
    pluginHostServiceInstantiated: instantiated("@mozilla.org/plugin/host;1"),
    blocklistInstantiated: instantiated("@mozilla.org/extensions/blocklist;1")
  }));
}
