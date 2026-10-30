/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Exercise current worker-read recovery and asynchronous write/rebuild errors.
// Reads now finish before any cookie API returns. The old partial-domain
// salvage races were removed by the OMT reader; failed reads discard the whole
// read array and preserve the malformed database as cookies.sqlite.bak.

const profile = do_get_profile();
const cookieFile = do_get_cookie_file(profile);
Services.prefs.setIntPref("network.cookie.cookieBehavior", 0);

function backupFile(suffix = ".bak") {
  let file = profile.clone();
  file.append("cookies.sqlite" + suffix);
  return file;
}

function cookie(host, value = "hai", name = "oh") {
  let now = Date.now() * 1000;
  return new Cookie(name, value, host, "/", Math.floor(now / 1e6) + 1000,
                    now, now, false, false, false);
}

function addCookie(value) {
  Services.cookiemgr.add(value.host, value.path, value.name, value.value,
    value.isSecure, value.isHttpOnly, value.isSession, value.expiry, {});
}

function observeTopic(topic, callback) {
  return new Promise((resolve, reject) => {
    let observer = {
      observe() {
        Services.obs.removeObserver(observer, topic);
        try {
          if (callback) {
            callback();
          }
          resolve();
        } catch (error) {
          reject(error);
        }
      }
    };
    Services.obs.addObserver(observer, topic, false);
    do_register_cleanup(() => {
      try {
        Services.obs.removeObserver(observer, topic);
      } catch (error) {}
    });
  });
}

function closeProfile() {
  let closed = observeTopic("cookie-db-closed");
  do_close_profile();
  return closed;
}

function loadProfile() {
  do_load_profile();
  return do_count_cookies();
}

function checkValue(host, expected) {
  let values = Services.cookiemgr.getCookiesFromHost(host, {});
  ok(values.hasMoreElements(), host + " has a cookie");
  equal(values.getNext().QueryInterface(Ci.nsICookie2).value, expected,
        host + " retains its cookie value");
  ok(!values.hasMoreElements(), host + " has exactly one cookie");
}

function checkDatabase(file, expectedCount) {
  let db = new CookieDatabaseConnection(file, COOKIE_DATABASE_SCHEMA_CURRENT);
  try {
    equal(db.db.schemaVersion, COOKIE_DATABASE_SCHEMA_CURRENT);
    equal(do_count_cookies_in_db(db.db), expectedCount);
  } finally {
    db.close();
  }
}

function cleanupFiles() {
  for (let file of [cookieFile, backupFile(), backupFile(".bak-rebuild")]) {
    if (file.exists()) {
      file.remove(false);
    }
    ok(!file.exists(), file.leafName + " was cleaned up");
  }
}

// Damage the actual table B-tree, retaining a valid SQLite header and schema.
// rootpage/page_size make this independent of allocator growth or row sizes.
function corruptTableRoot(file) {
  let db = Services.storage.openDatabase(file);
  let root = db.createStatement(
    "SELECT rootpage FROM sqlite_master WHERE name = 'moz_cookies'");
  let pages = db.createStatement("PRAGMA page_size");
  let rootPage, pageSize;
  try {
    ok(root.executeStep());
    rootPage = root.getInt32(0);
    ok(pages.executeStep());
    pageSize = pages.getInt32(0);
  } finally {
    root.finalize();
    pages.finalize();
    db.close();
  }
  ok(rootPage > 1, "cookie B-tree is separate from the schema/header page");
  let size = file.clone().fileSize;
  let stream = Cc["@mozilla.org/network/file-output-stream;1"]
                 .createInstance(Ci.nsIFileOutputStream);
  stream.init(file, 0x02, -1, 0);
  stream.QueryInterface(Ci.nsISeekableStream)
        .seek(Ci.nsISeekableStream.NS_SEEK_SET, (rootPage - 1) * pageSize);
  stream.write("\xff", 1); // No valid SQLite B-tree page has this page type.
  stream.close();
  equal(file.clone().fileSize, size, "corruption preserves file length");
  checkMalformedTable(file);
  return size;
}

function checkMalformedTable(file) {
  let db = Services.storage.openDatabase(file);
  let stmt;
  let failed = false;
  try {
    ok(db.tableExists("moz_cookies"), "schema remains readable");
    stmt = db.createStatement("SELECT * FROM moz_cookies");
    while (stmt.executeStep()) {}
  } catch (error) {
    equal(error.result, Cr.NS_ERROR_FILE_CORRUPTED,
          "table access reports genuine SQLite corruption");
    failed = true;
  } finally {
    if (stmt) {
      // sqlite3_finalize repeats the preceding step's error until reset.
      // The expected corruption was asserted above; clear that error while
      // keeping unexpected finalize/close failures visible.
      stmt.reset();
      stmt.finalize();
    }
    db.close();
  }
  ok(failed, "the corrupted table cannot be read");
}

add_task(function* initialize() {
  equal(do_count_cookies(), 0, "fresh profile starts empty");
  yield closeProfile();
  cleanupFiles();
});

add_task(function* test_async_constraint_failure_preserves_memory() {
  equal(loadProfile(), 0);
  addCookie(cookie("foo.com"));
  yield closeProfile();
  equal(loadProfile(), 1);

  // An out-of-band row conflicts with the service's subsequent async INSERT.
  let db = new CookieDatabaseConnection(cookieFile, COOKIE_DATABASE_SCHEMA_CURRENT);
  db.insertCookie(cookie("bar.com", "stale"));
  db.close();
  let rebuilding = observeTopic("cookie-db-rebuilding");
  addCookie(cookie("bar.com", "hallo"));
  equal(do_count_cookies(), 2, "failed disk write does not reject the cookie");
  yield rebuilding;
  equal(do_count_cookies(), 2, "all in-memory cookies survive rebuilding");
  yield closeProfile();

  checkDatabase(backupFile(), 2);
  checkDatabase(cookieFile, 2);
  equal(loadProfile(), 2);
  checkValue("foo.com", "hai");
  checkValue("bar.com", "hallo");
  yield closeProfile();
  cleanupFiles();
});

add_task(function* test_worker_read_corruption_is_backed_up() {
  equal(loadProfile(), 0);
  for (let i = 0; i < 20; ++i) {
    addCookie(cookie(i + ".com"));
  }
  yield closeProfile();
  let size = corruptTableRoot(cookieFile);

  // A failed whole-table read is discarded, not exposed as a partial domain
  // set. A synchronous API call waits for the worker's retry and fresh DB.
  equal(loadProfile(), 0, "failed worker read exposes no partial cookie set");
  ok(backupFile().exists(), "malformed original is retained");
  equal(backupFile().fileSize, size);
  checkMalformedTable(backupFile());
  addCookie(cookie("recovered.com"));
  yield closeProfile();
  checkDatabase(cookieFile, 1);
  equal(loadProfile(), 1);
  checkValue("recovered.com", "hai");
  yield closeProfile();
  cleanupFiles();
});

add_task(function* test_queued_write_errors_rebuild_once() {
  equal(loadProfile(), 0);
  addCookie(cookie("foo.com"));
  yield closeProfile();
  equal(loadProfile(), 1);
  let db = new CookieDatabaseConnection(cookieFile, COOKIE_DATABASE_SCHEMA_CURRENT);
  db.insertCookie(cookie("bar.com", "stale"));
  db.insertCookie(cookie("baz.com", "stale"));
  db.close();

  let rebuilding = observeTopic("cookie-db-rebuilding");
  // Both writes are queued before the error callbacks can run. An additional
  // old-connection failure while closing must not abort the new rebuild.
  addCookie(cookie("bar.com", "new-bar"));
  addCookie(cookie("baz.com", "new-baz"));
  yield rebuilding;
  equal(do_count_cookies(), 3);
  yield closeProfile();
  ok(!backupFile(".bak-rebuild").exists(), "the single rebuild succeeds");
  checkDatabase(backupFile(), 3);
  checkDatabase(cookieFile, 3);
  equal(loadProfile(), 3);
  checkValue("bar.com", "new-bar");
  checkValue("baz.com", "new-baz");
  yield closeProfile();
  cleanupFiles();
});

add_task(function* test_rebuild_insert_failure_keeps_both_backups() {
  equal(loadProfile(), 0);
  addCookie(cookie("foo.com"));
  yield closeProfile();
  equal(loadProfile(), 1);
  let db = new CookieDatabaseConnection(cookieFile, COOKIE_DATABASE_SCHEMA_CURRENT);
  db.insertCookie(cookie("bar.com", "stale"));
  db.close();

  let closed = observeTopic("cookie-db-closed");
  let rebuilding = observeTopic("cookie-db-rebuilding", () => {
    // The synchronous observer runs after the new DB exists but before its
    // write connection/rebuild INSERT. Force a real unique constraint error.
    let rebuilt = new CookieDatabaseConnection(cookieFile, COOKIE_DATABASE_SCHEMA_CURRENT);
    rebuilt.insertCookie(cookie("bar.com", "collision"));
    rebuilt.close();
  });
  addCookie(cookie("bar.com", "hallo"));
  yield rebuilding;
  yield closed;

  ok(backupFile().exists(), "original failed database is preserved");
  ok(backupFile(".bak-rebuild").exists(), "failed rebuild is preserved separately");
  ok(!cookieFile.exists(), "failed rebuild is not used as the active DB");
  checkDatabase(backupFile(), 2);
  checkDatabase(backupFile(".bak-rebuild"), 1);
  equal(do_count_cookies(), 2, "rebuild failure retains in-memory cookies");
  checkValue("foo.com", "hai");
  checkValue("bar.com", "hallo");
  // No write connection remains, so closing needs no async completion event.
  do_close_profile();
  cleanupFiles();
});
