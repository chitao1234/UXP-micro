Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");

var Cc = Components.classes;
var Ci = Components.interfaces;

function hasASLR(file) {
  let input = Cc["@mozilla.org/network/file-input-stream;1"]
                .createInstance(Ci.nsIFileInputStream);
  input.init(file, 0x01, 0, 0);
  let binary = Cc["@mozilla.org/binaryinputstream;1"]
                 .createInstance(Ci.nsIBinaryInputStream);
  binary.setInputStream(input);
  let bytes;
  try {
    bytes = new Uint8Array(binary.readByteArray(binary.available()));
  } finally {
    binary.close();
  }
  let header = new DataView(bytes.buffer);
  equal(header.getUint16(0, true), 0x5a4d, "The fixture has a DOS header");
  let pe = header.getUint32(0x3c, true);
  equal(header.getUint32(pe, true), 0x4550, "The fixture has a PE header");
  return !!(header.getUint16(pe + 24 + 70, true) & 0x40);
}

function run_test() {
  ok(!hasASLR(do_get_file('components/testcompnoaslr.dll')),
     "The negative fixture is actually linked without ASLR");
  ok(hasASLR(do_get_file('components/testcomponent.dll')),
     "The control fixture supports ASLR");
  let manifest = do_get_file('components/testcompnoaslr.manifest');
  registerAppManifest(manifest);
  var sysInfo = Cc["@mozilla.org/system-info;1"].
                getService(Ci.nsIPropertyBag2);
  var ver = parseFloat(sysInfo.getProperty("version"));
  if (ver < 6.0) {
    // This is disabled on pre-Vista OSs.
    do_check_true("{335fb596-e52d-418f-b01c-1bf16ce5e7e4}" in Components.classesByID);
  } else {
    do_check_false("{335fb596-e52d-418f-b01c-1bf16ce5e7e4}" in Components.classesByID);
  }
  registerAppManifest(do_get_file('components/testcomponent.manifest'));
  do_check_true("{335fb596-e52d-418f-b01c-1bf16ce5e7e4}" in Components.classesByID);
}
