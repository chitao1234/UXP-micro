# OS.File atomic-write and lifecycle coverage

These six tests and their head come from Firefox 78 revision
`600df6aa643726626d2eab36bc0f6e34bbe5ac2a`, under the same directory.
UXP `0ddd00f195` deleted the original toolkit suites in 2020. Current Firefox
no longer has OS.File, so its replacement I/O tests are not an API oracle here.

The selected tests retain every upstream assertion for backup options, empty
writes, Unicode paths, queue success/failure, worker reset/resource ownership,
and shutdown warnings. The existing native/worker pairs remain paired; no
Windows arm is skipped. The manifest selects only these supported APIs.

Harness adaptations use explicit component aliases and `Cu.import`,
`XPCOMUtils.defineLazyModuleGetter`, `do_print`, `do_execute_soon`, and the
older three-argument observer notification. Profile-owned backup paths avoid
shared temporary-directory collisions. Cleanup preserves prior preferences,
closes opened resources even after failures, and unregisters shutdown listeners.

Labeled local cleanup/helper repairs:

- Close the queue success test's file handle instead of leaking it.
- Clear the actual `toolkit.asyncshutdown.testing` preference, correcting the
  upstream `toolkit.async_shutdown.testing` typo.
- Do not shadow the Promise rejection callback in `reference_fetch_file`.
  That helper is preserved for upstream head compatibility; these six tests
  do not claim to exercise it.

Original Firefox 78 SHA256 values:

| File | SHA256 |
| --- | --- |
| head.js | 1deb97011ce7848e76b3d2b1485920c1f596c14c8e13ffc190a38b7d7dca8ec7 |
| test_osfile_writeAtomic_backupTo_option.js | 29e7fa3d383e075e2ac32f3e3fe459a1e64001625d3f341736c19a67e463c467 |
| test_osfile_writeAtomic_zerobytes.js | de86b7efda06ffc4aea1ecf60f1fcaf637bbd76c492aaf1577a4c03d3dc7aa45 |
| test_osfile_writeAtomic_unicode_filename.js | 4659fb1fae09020afdeb612019c4465938f417a01b389ffd4be460bd1145be6d |
| test_queue.js | 9891402df0adb34f2856c66b22d90eb82a69f705f596c45298c48f711f85b235 |
| test_reset.js | 1585a5b04b04163af1e38d7faa69abb1b61aaaaaed5bfd9de3eacd752e5861fc |
| test_shutdown.js | 288fed58865f93ff8893cbd0f9026be3367a7afc288df3db717335a01fbceb85 |
| xpcshell.ini (original full manifest) | e705158b94e563efffb495e8d9645549451ea19c38183367d3febed19cf88e84 |
