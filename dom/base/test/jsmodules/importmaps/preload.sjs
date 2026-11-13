// A parser-blocking response lets the speculative loader reach the following
// module before the parser registers its import map. No external services.
var timers = new Set();

function handleRequest(request, response) {
  response.setHeader("Content-Type", "application/javascript", false);
  response.setHeader("Cache-Control", "no-store", false);
  response.processAsync();
  const timer = Components.classes["@mozilla.org/timer;1"]
    .createInstance(Components.interfaces.nsITimer);
  timers.add(timer);
  timer.initWithCallback(() => {
    if (request.queryString === "module") {
      response.write("window.slowEvaluations=(window.slowEvaluations||0)+1; export const value=42;\n");
    } else if (request.queryString === "steal") {
      response.write('var s=document.createElement("script"); s.type="module"; ' +
        's.crossOrigin="use-credentials"; s.src=new URL("preload.sjs?module",document.baseURI).href; ' +
        's.onload=function(){window.stealLoaded=true;}; s.onerror=function(){window.stealError=true;}; ' +
        'document.head.appendChild(s);');
    } else {
      response.write("// Parser may now continue.\n");
    }
    response.finish();
    timers.delete(timer);
  }, request.queryString === "module" ? 500 : 250, Components.interfaces.nsITimer.TYPE_ONE_SHOT);
}
