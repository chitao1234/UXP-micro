/* -*- indent-tabs-mode: nil; js-indent-level: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// explicitly run on ECMA 5
version(185);

var appendToActual = function(s) {
    actual += s + ',';
}

// Never count an unsupported stress operation as a successful test. Keep a
// fallback for tests which call these unconditionally, but report an explicit
// capability skip when the operation is actually requested.
var jitTestMissingCapabilities = {};
for (var name of ["gczeal", "schedulegc", "gcslice", "selectforgc",
                  "verifyprebarriers", "verifypostbarriers", "gcPreserveCode"]) {
  if (!(name in this)) {
    jitTestMissingCapabilities[name] = true;
    this[name] = (function(name) {
      return function() {
        print("JIT-TEST-UNSUPPORTED: " + name);
        quit(59);
      };
    })(name);
  }
}
