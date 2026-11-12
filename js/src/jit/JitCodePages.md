# Executable pool backing experiment

This opt-in architecture keeps compiler thresholds, generated code, IC feedback,
code addresses and the process executable-reservation limit unchanged. It tracks
OS-page owners inside bump-allocated executable pools. New pools reserve their
usual address range but commit pages on allocation; a natural JIT poison batch
decommits complete pages with no surviving or pending-poison allocation.

`javascript.options.mem.jit_code_decommit` is **false by default**. The standalone
shell accepts `MOZ_JIT_CODE_DECOMMIT=1`; `jitCodeDecommit([bool])` supports live
policy checks. A future tier default requires real-page memory and performance
qualification. Already decommitted pools remain valid if the policy is disabled.

Page ownership includes the complete JitCode header and allocation padding.
Foreground JITCODE finalization queues ownership retirement with the existing
poison write. Every write in the batch finishes before any ownership is removed;
then the normal W^X transition skips physically unbacked spans. Allocation and
retirement are protected by AutoPreventBackedgePatching. PerfEnabled's intentional
code leaks retain their owners. Failed poison-queue allocation can retire an
unreachable range immediately because no delayed writer was registered.

Physical recommit is fallible. A failed allocation does not advance its cursor,
add page owners, charge code-kind bytes or return a pool reference. A page-map or
pool-index allocation failure releases the entire executable reservation.
Partially successful commits remain accurately marked and can be reclaimed by a
later natural poison batch. Live allocation boundaries never move or reuse holes.

`runtime/code/unused` reports committed unused bytes; `pool-metadata` includes the
page counts, pool records and indices. Alternate `js-jit-code/...` reports expose
reserved, decommitted, reclaimable (committed whole zero-owner pages), and pool
count. These alternate views must not be added to explicit memory. They are the
eligibility census: a large unused total can be pinned by mixed live/dead pages.

The native `testJitCodePages*` cases check boundary sharing, delayed retirement,
W^X and Windows MEM_RESERVE state, complete decommit, tail recommit, policy
transitions, deliberate non-retirement, pool initialization OOM, GC-cell OOM and
physical-recommit OOM. `gc/jit-code-pages.js` exercises real JIT/IC, debugger and
incremental/compacting GC lifetimes across policy changes.

Before default-enabling, run one matched binary on cold AB and reverse-order warm
BA profiles with the same initialized WordPress embeds and a GitHub guardrail.
Sample OS counters at process ages 45/90 s before an unforced built-in report.
Extend 180/360 s only for a demonstrated retention question. Require at least
4 MiB repeatable metadata-inclusive attributed and private-commit reduction on
the embed-heavy page, beyond pair spread; GitHub must have no repeatable >1 MiB
regression. Check whole decommitted/reclaimable pages against OS commit state;
do not attribute GC population changes or reservation-only deltas to this code.
No functional differences or live-code/feedback loss are allowed. Investigate a
repeatable >=3% CPU/throughput or >=10% p95 interaction/compilation latency cost.
If real pages have less than 4 MiB reclaimable backing, stop before a campaign.
