# Native profiler restoration

Build Windows x86/x64 diagnostics with `--enable-profiler`. The option is off
by default. Test manifests use the generated `profiler` capability in mozinfo;
an enabled build must register `nsIProfiler` and its service.

The sampling backend and Gecko hooks originate in the parent of UXP
`ac25827a87`, which removed SPS. The JS engine still provides the matching
profiling-stack and frame-iterator APIs. Native start/stop and feature flags use
Mozilla atomics. Captured PC, SP and FP are passed to SpiderMonkey, matching
Firefox78's sampler and this tree's x86 Wasm frame-iterator contract.

The supported browser configuration is single process. The restored backend
profiles its registered main, compositor and worker threads and exposes the
existing DevTools profiler/performance actors. The historical profile JSON
format is retained for those actors. Subprocess profile aggregation and native
backends for other operating systems remain outside this restoration.

Keep shipping enablement separate from diagnostic availability: inactive
thread registration and labels have a cost even when no sampling buffer or
sampling thread exists. Qualify that cost before changing shipping defaults.

Focused validation lives in `tests/xpcshell.ini`, `tests/gtest`, the nine
`devtools/server/tests/unit/test_profiler_*` parents, and the enabled performance
panel parents in `devtools/client/performance/test/browser.ini`. Retain inherited
upstream skips. The test head rejects absent interfaces/services to avoid
qualifying tests that simply return early.
