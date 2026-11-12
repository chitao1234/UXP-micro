/* MPL-2.0. Request counters for native lazy iframe navigation tests. */
function handleRequest(request, response) {
  const query = {};
  for (const pair of request.queryString.split("&")) {
    const parts = pair.split("=");
    query[parts[0]] = decodeURIComponent(parts.slice(1).join("="));
  }
  response.setHeader("Cache-Control", "no-store", false);
  const key = "lazy-frame-" + query.id;
  if (query.action === "status") {
    response.setHeader("Content-Type", "application/json", false);
    response.write(getState(key) || "null");
    return;
  }
  const old = JSON.parse(getState(key) || '{"count":0}');
  setState(key, JSON.stringify({count: old.count + 1,
    referrer: request.hasHeader("Referer") ? request.getHeader("Referer") : ""}));
  function finish() {
    response.setHeader("Content-Type", query.action === "image" ?
                       "image/svg+xml" : "text/html", false);
    if (query.action === "image") {
      response.write('<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"/>');
    } else if (query.action === "page") {
      const base = "file_iframe_loading_lazy.sjs?id=" + query.id;
      response.write('<!doctype html><meta charset="utf-8">' +
        '<script>window.loaded=false;onload=()=>window.loaded=true;</script>' +
        '<style>body{margin:0;height:40000px}iframe{width:200px;height:100px}</style>' +
        '<img src="' + base + '-blocker&action=image&delay=250">' +
        '<iframe id="near" loading="lazy" src="' + base + '-near&delay=1500"></iframe>' +
        '<iframe id="far" name="lazy-target" src="' + base + '-far" loading="lazy" ' +
        'style="position:absolute;top:20000px"></iframe>');
    } else {
      response.write('<!doctype html><meta charset="utf-8"><body style="background:#008000">' +
        '<p id="value">' + query.id + '</p><script>window.executed=true;' +
        'onunload=()=>parent.postMessage("unload:"+' + JSON.stringify(query.id) + ',"*");' +
        'parent.postMessage("loaded:"+' + JSON.stringify(query.id) + ',"*");</script>');
    }
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
