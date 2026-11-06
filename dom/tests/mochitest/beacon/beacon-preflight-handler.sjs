/* Test the actual OPTIONS/POST boundary. State is isolated per test token. */
const BinaryInputStream = Components.Constructor(
  "@mozilla.org/binaryinputstream;1", "nsIBinaryInputStream", "setInputStream");

function handleRequest(request, response) {
  response.setHeader("Cache-Control", "no-cache, must-revalidate", false);
  const params = {};
  for (const field of request.queryString.split("&")) {
    const pair = field.split("=");
    params[decodeURIComponent(pair[0])] = decodeURIComponent(pair[1] || "");
  }
  const id = params.id;
  const operation = params.op;
  const key = "beacon-cors-" + id;
  function state() {
    return JSON.parse(getState(key) || '{"options":0,"posts":0}');
  }
  function reply(target, data) {
    target.setHeader("Content-Type", "application/json", false);
    target.write(JSON.stringify(data));
  }
  function resolveWait(name, data) {
    getObjectState(key + name, function(waiter) {
      if (waiter) {
        setObjectState(key + name, null);
        reply(waiter, data);
        waiter.finish();
      }
    });
  }
  if (operation == "reset") {
    setState(key, '{"options":0,"posts":0}');
    reply(response, state());
    return;
  }
  const data = state();
  if (operation == "observe-no-post") {
    // WPT observes three seconds after a rejected preflight. Keep that window
    // at the HTTP sink, independent of the test document's timer interception.
    response.processAsync();
    const timer = Components.classes["@mozilla.org/timer;1"]
      .createInstance(Components.interfaces.nsITimer);
    timer.initWithCallback(function() {
      reply(response, state());
      response.finish();
      timer.cancel();
    }, 3000, Components.interfaces.nsITimer.TYPE_ONE_SHOT);
    return;
  }
  if (operation == "counts") {
    reply(response, data);
    setState(key, "");
    return;
  }
  if (operation == "wait-post" || operation == "wait-options") {
    const count = operation == "wait-post" ? data.posts : data.options;
    if (count) {
      reply(response, data);
    } else {
      response.processAsync();
      setObjectState(key + operation, response);
    }
    return;
  }
  if (operation != "beacon") {
    response.setStatusLine(request.httpVersion, 400, "Bad Request");
    return;
  }
  if (request.hasHeader("origin")) {
    response.setHeader("Access-Control-Allow-Origin", request.getHeader("origin"), false);
  }
  response.setHeader("Access-Control-Allow-Methods", "POST", false);
  response.setHeader("Access-Control-Allow-Headers", "content-type", false);
  if (params.mode != "no-credentials") {
    response.setHeader("Access-Control-Allow-Credentials", "true", false);
  }
  if (request.method == "OPTIONS") {
    ++data.options;
    data.origin = request.getHeader("origin");
    data.requestMethod = request.getHeader("access-control-request-method");
    data.requestHeaders = request.getHeader("access-control-request-headers");
    setState(key, JSON.stringify(data));
    if (params.mode == "deny") {
      response.setStatusLine(request.httpVersion, 403, "Forbidden");
    }
    resolveWait("wait-options", data);
  } else if (request.method == "POST") {
    ++data.posts;
    data.contentType = request.hasHeader("content-type") ? request.getHeader("content-type") : "";
    const stream = new BinaryInputStream(request.bodyInputStream);
    data.bytes = [];
    while (stream.available()) {
      data.bytes.push(...stream.readByteArray(stream.available()));
    }
    setState(key, JSON.stringify(data));
    resolveWait("wait-post", data);
  }
}
