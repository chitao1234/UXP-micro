// |jit-test| skip-if: !this.oomTest; allow-oom
// The shell reports job-queue exceptions through AutoReportException. A job
// that hits the deliberate OOM may therefore print "out of memory" even when
// oomTest has reset injection. Native assertions/crashes still fail this test.
async function* values() { yield 1; yield 2; }
oomTest(function() {
    let generator = values();
    for (let i = 0; i < 6; i++)
        generator.next();
    drainJobQueue();
});
