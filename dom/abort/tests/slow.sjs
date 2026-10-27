/* Delayed headers/body for cancellation tests. */
function handleRequest(request, response) {
  response.processAsync();
  response.setHeader("Content-Type", "text/plain", false);
  response.setHeader("Access-Control-Allow-Origin", "*", false);
  response.setHeader("Cache-Control", "no-store", false);
  if (request.queryString.indexOf("null") >= 0) {
    response.setStatusLine(request.httpVersion, 204, "No Content");
    response.finish(); return;
  }
  if (request.queryString.indexOf("body") >= 0) {
    response.write("first chunk\n");
  }
  var timer = Components.classes["@mozilla.org/timer;1"].createInstance(Components.interfaces.nsITimer);
  timer.initWithCallback(function() {
    try { response.write("last chunk\n"); response.finish(); } catch (e) {}
  }, 200, Components.interfaces.nsITimer.TYPE_ONE_SHOT);
}
