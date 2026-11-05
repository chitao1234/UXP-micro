/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// These regressions exercise preview documents, not native printer drivers.
// Use the PDF settings already used by printpreview_bug482976_helper.xul.
function createTestPrintPreviewSettings() {
  const Cc = Components.classes;
  const Ci = Components.interfaces;
  let settings = Cc["@mozilla.org/gfx/printsettings-service;1"]
    .getService(Ci.nsIPrintSettingsService).newPrintSettings;
  settings.outputFormat = Ci.nsIPrintSettings.kOutputFormatPDF;
  settings.showPrintProgress = false;
  let output = Cc["@mozilla.org/file/directory_service;1"]
    .getService(Ci.nsIProperties).get("TmpD", Ci.nsIFile);
  output.append("printpreview-regression.pdf");
  output.createUnique(Ci.nsIFile.NORMAL_FILE_TYPE, 0o600);
  // A preview viewer can keep the file stream alive until destruction.
  Cc["@mozilla.org/uriloader/external-helper-app-service;1"]
    .getService(Ci.nsPIExternalAppLauncher).deleteTemporaryFileOnExit(output);
  settings.toFileName = output.path;
  return settings;
}
