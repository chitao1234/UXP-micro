/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

function handleRequest(request, response) {
  response.setStatusLine(request.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "text/plain", false);
  response.setHeader("Cache-Control", "no-store", false);

  if (request.queryString === "hsts") {
    response.setHeader("Strict-Transport-Security", "max-age=60", false);
  } else if (request.queryString === "hpkp") {
    response.setHeader("Public-Key-Pins",
      'max-age=60; pin-sha256="VCIlmPM9NkgFQtrs4Oa5TeFcDu6MWRTKSNdePEhOgD8="; ' +
      'pin-sha256="AAAAAAAAAAAAAAAAAAAAAAAAAj0e1Md7GkYYkVoZWmM="', false);
  }

  response.write("Network security header fixture");
}
