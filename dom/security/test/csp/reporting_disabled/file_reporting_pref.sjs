// The WPT report-and-enforce fixture observes blocked and allowed resources.
// This retained-fork pref control uses a real keyed report sink as well.
const PNG = atob("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGNg+M/wHwAEAQH/cetH5QAAAABJRU5ErkJggg==");

function stateFor(token) {
  const value = getState(token);
  return value ? JSON.parse(value) : {reports: 0, allowed: 0, blocked: 0};
}

function writeJSON(response, value) {
  response.setHeader("Content-Type", "application/json", false);
  response.write(JSON.stringify(value));
}

function handleRequest(request, response) {
  response.setHeader("Cache-Control", "no-store", false);
  const query = {};
  for (const field of request.queryString.split("&")) {
    const pair = field.split("=");
    query[decodeURIComponent(pair[0])] = decodeURIComponent(pair[1] || "");
  }
  const token = query.token;
  if (!token || !/^[a-z0-9-]+$/.test(token)) {
    response.setStatusLine(request.httpVersion, 400, "Invalid token");
    return;
  }
  let state = stateFor(token);
  if (query.op == "reset") {
    state = {reports: 0, allowed: 0, blocked: 0};
    setState(token, JSON.stringify(state));
    writeJSON(response, state);
    return;
  }
  if (query.op == "counts") {
    writeJSON(response, state);
    return;
  }
  if (query.op == "wait") {
    if (state.reports) {
      writeJSON(response, state);
    } else {
      response.processAsync();
      setObjectState("wait-" + token, response);
    }
    return;
  }
  if (query.op == "report") {
    state.reports++;
    state.reportMethod = request.method;
    setState(token, JSON.stringify(state));
    getObjectState("wait-" + token, function(waiter) {
      if (waiter) {
        setObjectState("wait-" + token, null);
        writeJSON(waiter, state);
        waiter.finish();
      }
    });
    response.setStatusLine(request.httpVersion, 204, "No Content");
    return;
  }
  if (query.op == "image") {
    state[query.kind]++;
    setState(token, JSON.stringify(state));
    response.setHeader("Content-Type", "image/png", false);
    response.write(PNG);
    return;
  }
  if (query.op == "page") {
    const base = "http://mochi.test:8888/tests/dom/security/test/csp/reporting_disabled/file_reporting_pref.sjs";
    response.setHeader("Content-Type", "text/html", false);
    response.setHeader("Content-Security-Policy",
      "default-src 'none'; script-src 'unsafe-inline'; img-src 'self'; report-uri " +
      base + "?op=report&token=" + token, false);
    response.write("<!DOCTYPE html><script>" +
      "for (const kind of ['allowed', 'blocked']) {" +
      "const image = new Image();" +
      "image.onload = image.onerror = function(event) {" +
      "parent.postMessage({token:" + JSON.stringify(token) + ",kind:kind," +
      "type:event.type,width:image.naturalWidth}, 'http://mochi.test:8888');};" +
      "document.documentElement.appendChild(image);" +
      "image.src = (kind == 'allowed' ? " + JSON.stringify(base) + " : " +
      JSON.stringify(base.replace("mochi.test:8888", "example.com")) + ") +" +
      "'?op=image&token=" + token + "&kind=' + kind;}" +
      "</script>");
    return;
  }
  response.setStatusLine(request.httpVersion, 400, "Invalid operation");
}
