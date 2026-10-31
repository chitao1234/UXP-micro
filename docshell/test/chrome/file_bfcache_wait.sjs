/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

function handleRequest(request, response) {
  response.setHeader("Content-Type", "text/plain", false);
  response.setHeader("Cache-Control", "no-store", false);
  if (request.queryString === "hold") {
    response.processAsync();
    setObjectState("bfcache-readiness-response", response);
    response.write("Waiting for an explicit release.\n");
    return;
  }
  getObjectState("bfcache-readiness-response", held => {
    if (held) {
      held.finish();
      setObjectState("bfcache-readiness-response", null);
    }
  });
  response.write("Released.\n");
}
