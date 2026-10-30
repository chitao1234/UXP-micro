// |jit-test| module
// Exercise the actual shell loader, including relative resolution, metadata,
// and completion/rejection of dynamic imports that contain top-level await.
let loaded = false;
let failure;
import("loader-relative/main.js").then(ns => {
    assertEq(ns.value, 42);
    assertEq(/[\\/]loader-relative[\\/]main\.js$/.test(ns.url), true);
    loaded = true;
}, error => { failure = error; });
drainJobQueue();
assertEq(failure, undefined);
assertEq(loaded, true);
let firstNamespace;
import("loader-relative/dependency.js").then(ns => { firstNamespace = ns; });
drainJobQueue();
let aliasNamespace;
import("loader-relative/../loader-relative/./dependency.js").then(ns => { aliasNamespace = ns; });
drainJobQueue();
assertEq(firstNamespace !== undefined, true);
assertEq(firstNamespace, aliasNamespace);

let rejected = false;
import("loader-relative/reject.js").then(
    () => { throw new Error("Import unexpectedly fulfilled"); },
    error => { assertEq(error, "module rejection"); rejected = true; });
drainJobQueue();
assertEq(rejected, true);
