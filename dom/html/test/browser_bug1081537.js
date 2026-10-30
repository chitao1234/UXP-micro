// This test is useful because mochitest-browser runs as an addon, so we test
// addon-scope paths here.
var ifr;
function test() {
  ifr = document.createElement('iframe');
  document.getElementById('main-window').appendChild(ifr);
  let doc = ifr.contentDocument;
  is(doc.nodePrincipal.origin, "[System Principal]", "The iframe is privileged");
  let shell = ifr.contentWindow.QueryInterface(Ci.nsIInterfaceRequestor)
    .getInterface(Ci.nsIDocShell);
  let root = shell.sameTypeRootTreeItem.QueryInterface(Ci.nsIWebNavigation);
  is(root.sessionHistory, null, "The chrome subtree has no root session history");
  for (let value of ["first", "second"]) {
    is(doc.open(), doc, "document.open succeeds without session history");
    doc.write("<!doctype html><body>" + value + "</body>");
    doc.close();
    is(doc.body.textContent, value, "The opened document accepts new markup");
    is(ifr.contentDocument, doc, "document.open retains the document");
    is(doc.nodePrincipal.origin, "[System Principal]", "The principal is unchanged");
  }
}
registerCleanupFunction(() => ifr.remove());
