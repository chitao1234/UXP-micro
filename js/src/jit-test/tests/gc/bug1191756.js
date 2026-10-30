// |jit-test| skip-if: !this.oomTest
if (typeof oomAtAllocation === 'undefined')
    quit();

function fn(i) {
    if (i == 3)
      return ["isFinite"].map(function (i) {});
    return [];
}

// Exercise every allocation failure with the native harness, which resets
// failure injection before reporting or running the next iteration.
oomTest(function() {
    fn(0);
    fn(1);
    fn(2);
    fn(3);
});
