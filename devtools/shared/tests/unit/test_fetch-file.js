/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

// Tests for DevToolsUtils.fetch on file:// URI's.

const { FileUtils } = Cu.import("resource://gre/modules/FileUtils.jsm", {});
const { OS } = Cu.import("resource://gre/modules/osfile.jsm", {});
const Services = require("Services");

const TEST_CONTENT = "aéd";

// The TEST_CONTENT encoded as UTF-8.
const UTF8_TEST_BUFFER = new Uint8Array([0x61, 0xc3, 0xa9, 0x64]);

// The TEST_CONTENT encoded as ISO 8859-1.
const ISO_8859_1_BUFFER = new Uint8Array([0x61, 0xe9, 0x64]);

/**
 * Tests that URLs with arrows pointing to an actual source are handled properly
 * (bug 808960). For example 'resource://gre/modules/XPIProvider.jsm ->
 * file://l10n.js' should load 'file://l10n.js'.
 */
add_task(function* test_arrow_urls() {
  let file = createTemporaryFile(".js");
  let { path } = file;
  let url = "resource://gre/modules/XPIProvider.jsm -> " +
            Services.io.newFileURI(file).spec;

  yield OS.File.writeAtomic(path, TEST_CONTENT, { encoding: "utf-8" });
  let { content } = yield DevToolsUtils.fetch(url);

  deepEqual(content, TEST_CONTENT, "The file contents were correctly read.");
});

/**
 * Tests that empty files are read correctly.
 */
add_task(function* test_empty() {
  let file = createTemporaryFile();
  let { content } = yield DevToolsUtils.fetch(Services.io.newFileURI(file).spec);
  deepEqual(content, "", "The empty file was read correctly.");
});

/**
 * Tests that UTF-8 encoded files are correctly read.
 */
add_task(function* test_encoding_utf8() {
  let { path } = createTemporaryFile();
  yield OS.File.writeAtomic(path, UTF8_TEST_BUFFER);

  let { content } = yield DevToolsUtils.fetch(path);
  deepEqual(content, TEST_CONTENT,
    "The UTF-8 encoded file was correctly read.");
});

/**
 * Tests that ISO 8859-1 (Latin-1) encoded files are correctly read.
 */
add_task(function* test_encoding_iso_8859_1() {
  let { path } = createTemporaryFile();
  yield OS.File.writeAtomic(path, ISO_8859_1_BUFFER);

  let { content } = yield DevToolsUtils.fetch(path);
  deepEqual(content, TEST_CONTENT,
    "The ISO 8859-1 encoded file was correctly read.");
});

/**
 * Test that non-existent files are handled correctly.
 */
add_task(function* test_missing() {
  yield DevToolsUtils.fetch("file:///file/not/found.right").then(result => {
    do_print(result);
    ok(false, "Fetch resolved unexpectedly when the file was not found.");
  }, () => {
    ok(true, "Fetch rejected as expected because the file was not found.");
  });
});

/**
 * Test that URLs without file:// scheme work.
 */
add_task(function* test_schemeless_files() {
  let { path } = createTemporaryFile();

  yield OS.File.writeAtomic(path, TEST_CONTENT, { encoding: "utf-8" });

  let { content } = yield DevToolsUtils.fetch(path);
  deepEqual(content, TEST_CONTENT, "The content was correct.");
});

add_task(function* test_native_paths_and_url_delimiters() {
  let file = createTemporaryFile(" path # % \u00e9.js");
  yield OS.File.writeAtomic(file.path, TEST_CONTENT, { encoding: "utf-8" });
  let fileURL = Services.io.newFileURI(file).spec;
  let paths = [file.path, fileURL,
               "resource://gre/modules/XPIProvider.jsm -> " + file.path,
               "resource://gre/modules/XPIProvider.jsm -> " + fileURL];
  if (Services.appinfo.OS == "WINNT") {
    // Both backslashes and forward slashes are accepted in drive paths.
    paths.push(file.path.replace(/\\/g, "/"));
  }
  for (let path of paths) {
    let { content } = yield DevToolsUtils.fetch(path);
    equal(content, TEST_CONTENT, "Native/URL path reads the exact file: " + path);
  }
});

/**
 * Creates a temporary file that is removed after the test completes.
 */
function createTemporaryFile(extension) {
  let name = "test_fetch-file-" + Math.random() + (extension || "");
  let file = FileUtils.getFile("TmpD", [name]);
  file.create(Ci.nsIFile.NORMAL_FILE_TYPE, parseInt("0755", 8));

  do_register_cleanup(() => {
    file.remove(false);
  });

  return file;
}
