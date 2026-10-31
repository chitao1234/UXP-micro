/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// Adapted from current Firefox's bug 2067308
// netwerk/test/unit/test_transaction_restart_activity.js at 309a5a015112.
// Retain its hold/drop schedule and native timing assertions.  NodeHTTPSServer
// is absent here, so a loopback HTTP/1 server supplies actual persistent sockets.
// The reuse assertion below additionally rejects a vacuous fresh-connect pass.
Cu.import("resource://gre/modules/NetUtil.jsm");
Cu.import("resource://gre/modules/Services.jsm");

function makePersistentServer() {
  let server = Cc["@mozilla.org/network/server-socket;1"]
                 .createInstance(Ci.nsIServerSocket);
  server.init(-1, true, -1);
  let resolveHeld;
  let state = {
    connections: [],
    heldConnection: null,
    failedConnection: null,
    servingConnection: null,
    dropped: false,
    stopped: false,
    heldArrived: new Promise(resolve => { resolveHeld = resolve; }),
    port: server.port,
    close() {
      this.stopped = true;
      server.close();
      for (let connection of this.connections) {
        connection.transport.close(Cr.NS_BINDING_ABORTED);
      }
    }
  };

  function respond(connection, body) {
    let wire = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n" +
               "Content-Length: " + body.length + "\r\n" +
               "Cache-Control: no-store\r\nConnection: keep-alive\r\n\r\n" + body;
    equal(connection.output.write(wire, wire.length), wire.length,
       "the complete persistent response was written");
  }

  function handle(connection, path) {
    do_print("HTTP/1 request " + path + " on server connection " + connection.id);
    if (path === "/hold") {
      state.heldConnection = connection;
      resolveHeld();
    } else if (path === "/drop-pooled" && !state.dropped) {
      state.dropped = true;
      state.failedConnection = connection;
      // The timing is in the server arrangement, as in the upstream fixture.
      do_timeout(300, function() {
        if (state.stopped) {
          return;
        }
        respond(state.heldConnection, "held");
        do_timeout(200, function() {
          if (!state.stopped) {
            do_print("Dropping unanswered server connection " + connection.id);
            connection.transport.close(Cr.NS_ERROR_NET_RESET);
          }
        });
      });
    } else if (path === "/drop-pooled") {
      state.servingConnection = connection;
      respond(connection, "ok");
    } else {
      do_throw("Unexpected HTTP/1 path: " + path);
    }
  }

  server.asyncListen({
    QueryInterface: XPCOMUtils.generateQI([Ci.nsIServerSocketListener]),
    onSocketAccepted(socket, transport) {
      let connection = {
        id: state.connections.length + 1,
        transport,
        input: transport.openInputStream(0, 0, 0),
        output: transport.openOutputStream(Ci.nsITransport.OPEN_BLOCKING, 0, 0),
        buffered: ""
      };
      state.connections.push(connection);
      do_print("Accepted server connection " + connection.id);
      let pump = Cc["@mozilla.org/network/input-stream-pump;1"]
                   .createInstance(Ci.nsIInputStreamPump);
      connection.pump = pump;
      pump.init(connection.input, -1, -1, 0, 0, false);
      pump.asyncRead({
        QueryInterface: XPCOMUtils.generateQI([Ci.nsIStreamListener]),
        onStartRequest() {},
        onDataAvailable(request, context, stream, offset, count) {
          try {
            connection.buffered += NetUtil.readInputStreamToString(stream, count);
            let end;
            while ((end = connection.buffered.indexOf("\r\n\r\n")) !== -1) {
              let headers = connection.buffered.slice(0, end);
              connection.buffered = connection.buffered.slice(end + 4);
              let match = /^GET ([^ ]+) HTTP\/1\.[01]\r?\n/.exec(headers + "\r\n");
              ok(match, "the server received a complete HTTP/1 GET");
              handle(connection, match[1]);
            }
          } catch (error) {
            do_report_unexpected_exception(error, "persistent retry server");
          }
        },
        onStopRequest(request, context, status) {
          do_print("Server connection " + connection.id + " stopped: " + status);
        }
      }, null);
    },
    onStopListening() {}
  });
  return state;
}

function openTimedChannel(uri) {
  const chan = NetUtil.newChannel({uri, loadUsingSystemPrincipal: true})
                       .QueryInterface(Ci.nsIHttpChannel);
  chan.loadFlags |= Ci.nsIRequest.LOAD_BYPASS_CACHE;
  chan.QueryInterface(Ci.nsITimedChannel).timingEnabled = true;
  let done = new Promise(resolve => {
    chan.asyncOpen2(new ChannelListener((request, buffer) => resolve([request, buffer]),
                                       null, CL_ALLOW_UNKNOWN_CL));
  });
  do_register_cleanup(() => {
    if (chan.isPending()) {
      chan.cancel(Cr.NS_BINDING_ABORTED);
    }
  });
  return {chan, done};
}

add_task(async function test_restart_onto_pooled_connection_clears_timings() {
  // Plain HTTP enables pipelining by default here, unlike upstream HTTPS.
  // Disable it for the same two-independent-connections server arrangement.
  const pref = "network.http.pipelining";
  const hadUserPref = Services.prefs.prefHasUserValue(pref);
  const original = Services.prefs.getBoolPref(pref);
  Services.prefs.setBoolPref(pref, false);
  do_register_cleanup(() => {
    if (hadUserPref) {
      Services.prefs.setBoolPref(pref, original);
    } else {
      Services.prefs.clearUserPref(pref);
    }
  });

  const server = makePersistentServer();
  do_register_cleanup(() => server.close());
  const base = "http://127.0.0.1:" + server.port;
  const held = openTimedChannel(base + "/hold").done;
  await server.heldArrived;

  const openedAt = Date.now() * 1000;
  const AFTER_DROP_US = 400000;
  const {chan, done} = openTimedChannel(base + "/drop-pooled");
  const timedChannel = chan.QueryInterface(Ci.nsITimedChannel);
  const [, buffer] = await done;
  const [, heldBuffer] = await held;

  do_print("connectStart=" + timedChannel.connectStartTime +
       " domainLookupStart=" + timedChannel.domainLookupStartTime +
       " requestStart=" + timedChannel.requestStartTime);
  equal(buffer, "ok", "the retried request succeeded");
  equal(heldBuffer, "held", "the held connection was released successfully");
  notEqual(server.failedConnection.id, server.heldConnection.id,
           "the failed request occupied a second connection");
  equal(server.servingConnection.id, server.heldConnection.id,
        "the retry actually reused the released persistent connection");
  ok(timedChannel.requestStartTime >= openedAt + AFTER_DROP_US,
     "requestStart is the retry's, not the failed attempt's");
  for (const field of ["connectStartTime", "domainLookupStartTime"]) {
    const value = timedChannel[field];
    ok(value === 0 || value >= openedAt + AFTER_DROP_US,
       field + " is not left over from the failed attempt");
  }
});
