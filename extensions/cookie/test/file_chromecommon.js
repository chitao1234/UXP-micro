let { classes: Cc, utils: Cu, interfaces: Ci } = Components;

let cs = Cc["@mozilla.org/cookiemanager;1"]
           .getService(Ci.nsICookieManager2);

addMessageListener("getCookieCountAndClear", () => {
  let count = 0;
  let cookies = [];
  for (let list = cs.enumerator; list.hasMoreElements();) {
    let cookie = list.getNext().QueryInterface(Ci.nsICookie2);
    cookies.push({host: cookie.host, path: cookie.path, name: cookie.name});
    ++count;
  }
  cs.removeAll();

  sendAsyncMessage("getCookieCountAndClear:return", { count, cookies });
});

cs.removeAll();
