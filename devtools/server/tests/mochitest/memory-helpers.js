var Cu = Components.utils;
var Cc = Components.classes;
var Ci = Components.interfaces;

var { require } = Cu.import("resource://devtools/shared/Loader.jsm", {});
const { Task } = require("devtools/shared/task");
var DevToolsUtils = require("devtools/shared/DevToolsUtils");
var Services = require("Services");
var { DebuggerClient } = require("devtools/shared/client/main");
var { DebuggerServer } = require("devtools/server/main");

var { MemoryFront } = require("devtools/shared/fronts/memory");

// Always log packets when running tests.
Services.prefs.setBoolPref("devtools.debugger.log", true);
SimpleTest.registerCleanupFunction(function () {
  Services.prefs.clearUserPref("devtools.debugger.log");
});

function startServerAndGetSelectedTabMemory() {
  SimpleTest.info("Memory helper: initializing server and connecting client");
  DebuggerServer.init();
  DebuggerServer.addBrowserActors();
  var client = new DebuggerClient(DebuggerServer.connectPipe());

  return client.connect()
    .then(() => {
      SimpleTest.info("Memory helper: connected; requesting tab list");
      return client.listTabs();
    })
    .then(response => {
      var form = response.tabs[response.selected];
      if (!form) {
        throw new Error("Memory helper: tab list has no selected tab");
      }
      var memory = MemoryFront(client, form, response);
      SimpleTest.info("Memory helper: ready for " + form.url +
                      "; memory actor " + form.memoryActor);

      return { memory, client };
    }).then(undefined, error => {
      SimpleTest.info("Memory helper: setup rejected: " +
                      DevToolsUtils.safeErrorString(error));
      throw error;
    });
}

function destroyServerAndFinish(client) {
  SimpleTest.info("Memory helper: closing client");
  client.close().then(() => {
    SimpleTest.info("Memory helper: client closed; destroying server");
    DebuggerServer.destroy();
    SimpleTest.finish();
  });
}

function waitForTime(ms) {
  return new Promise((resolve, reject) => {
    setTimeout(resolve, ms);
  });
}

function waitUntil(predicate) {
  if (predicate()) {
    return Promise.resolve(true);
  }
  return new Promise(resolve => setTimeout(() => waitUntil(predicate).then(() => resolve(true)), 10));
}
