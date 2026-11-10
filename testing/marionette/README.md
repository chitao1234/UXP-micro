Marionette tests require a tests-enabled browser build and the configured
Python 3 test virtualenv. Run them from the source root with:

```text
python mach marionette-test --disable-e10s --log-raw /path/to/results.jsonl --workspace /path/to/temporary-profile testing/marionette/harness/marionette_harness/tests/unit/test_capabilities.py
```

The command finds the configured browser binary; `--binary` can select another
matching tests build. With no test paths, it selects the core unit manifest.
Layout tests can be selected explicitly from `layout/base/tests/marionette`.
The same runner is available as `python -m marionette_harness.runtests`, with
explicit `--binary` and `--app fxdesktop` options. Python and JavaScript entries
use the real Marionette harness, including its intentional failure/timeout
selftests.

Preference overrides use `--pref=name:value`, for example
`--pref=layout.css.devPixelsPerPx:1.0`. `--socket-timeout 45` bounds a stalled
protocol command during focused diagnosis; it does not turn a timeout into
a pass.

`MARIONETTE-PARENT-START` and `MARIONETTE-PARENT-RESULT` log records identify
the selected file and its selected/completed case counts. The completion
record counts each case once, retaining its worst outcome if both the method
and teardown fail. Raw case-end events preserve diagnostics. A missing parent
result, a zero-case file, or skipped cases cannot establish a clean file pass.
B2G-only manifests must not be selected as desktop tests.

The desktop test profile keeps target=_blank links as tabs and scripted
window.open calls as windows, independently of application diversion defaults.
Tests that compare native recent-window order must temporarily disable
`focusmanager.testmode`, as the chrome window-switch tests do. The driver keeps
the real product name in its capabilities while using desktop browser window
and modal handling for all builds with `MOZ_BUILD_APP == "browser"`.
