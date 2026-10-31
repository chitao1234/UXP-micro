const button = document.getElementById("m");
document.addEventListener("securitypolicyviolation", function(event) {
  parent.postMessage({
    kind: "violation",
    blockedURI: event.blockedURI,
    violatedDirective: event.violatedDirective,
    sample: event.sample
  }, location.origin);
});

// Install the observer before this native inline-handler CSP check. Setting an
// event attribute uses EventListenerManager::GetAllowsInline, as in the original
// fixture, and a real click must still reach the external listener below.
button.setAttribute("onclick", "this.title='inline ran'");
button.addEventListener("click", function() {
  parent.postMessage({
    kind: "event",
    inlineRan: button.title == "inline ran"
  }, location.origin);
});
button.click();
