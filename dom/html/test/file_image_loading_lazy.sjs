/* MPL-2.0. Request counters make speculative and deferred loads observable. */
function handleRequest(request, response) {
  const query = {};
  for (const pair of request.queryString.split("&")) {
    const parts = pair.split("=");
    query[parts[0]] = decodeURIComponent(parts.slice(1).join("="));
  }
  response.setHeader("Cache-Control", query.cache ? "max-age=3600" : "no-store", false);
  if (query.action === "status") {
    response.setHeader("Content-Type", "application/json", false);
    response.write(getState("lazy-image-" + query.id) || "null");
    return;
  }
  if (query.action === "page") {
    response.setHeader("Content-Type", "text/html", false);
    const base = "file_image_loading_lazy.sjs?id=" + query.id;
    response.write('<!doctype html><meta charset="utf-8">' +
      '<script>window.loaded=false;onload=()=>window.loaded=true;</script>' +
      '<script src="' + base + '-script&action=script&delay=250"></script>' +
      '<style>body{margin:0;height:40000px}img{width:64px;height:64px}' +
      '#near{top:0}</style>' +
      '<img id="near" loading="lazy" src="' + base + '-near&delay=1200">' +
      '<img id="far" src="' + base + '-far" loading="lazy" style="position:absolute;top:20000px">');
    return;
  }
  if (query.action !== "script") {
    const key = "lazy-image-" + query.id;
    const old = JSON.parse(getState(key) || '{"count":0}');
    setState(key, JSON.stringify({count: old.count + 1,
      referrer: request.hasHeader("Referer") ? request.getHeader("Referer") : ""}));
  }
  function finish() {
    response.setHeader("Content-Type", query.action === "script" ?
      "text/javascript" : "image/svg+xml", false);
    response.write(query.action === "script" ? "/* parser delay */" :
      '<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64">' +
      '<rect width="64" height="64" fill="#008000"/></svg>');
    if (query.delay) response.finish();
  }
  if (query.delay) {
    response.processAsync();
    const timer = Components.classes["@mozilla.org/timer;1"]
      .createInstance(Components.interfaces.nsITimer);
    timer.initWithCallback(() => { finish(); timer.cancel(); },
      Number(query.delay), Components.interfaces.nsITimer.TYPE_ONE_SHOT);
  } else {
    finish();
  }
}
