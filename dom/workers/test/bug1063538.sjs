const { setTimeout } = Components.utils.import("resource://gre/modules/Timer.jsm", {});

function handleRequest(request, response) {
  response.processAsync();
  response.write("Hello");
  // The worker aborts on progress; this keeps onloadend from winning the race.
  setTimeout(function() { response.finish(); }, 100000);
}
