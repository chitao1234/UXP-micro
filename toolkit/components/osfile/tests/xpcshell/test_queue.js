"use strict";

const { OS } = Cu.import("resource://gre/modules/osfile.jsm", {});

// Check if Scheduler.queue returned by OS.File.queue is resolved initially.
add_task(async function check_init() {
  await OS.File.queue;
  do_print("Function resolved");
});

// Check if Scheduler.queue returned by OS.File.queue is resolved
// after an operation is successful.
add_task(async function check_success() {
  do_print("Attempting to open a file correctly");
  let openedFile = await OS.File.open(OS.Path.join(do_get_cwd().path, "test_queue.js"));
  try {
    do_print("File opened correctly");
    await OS.File.queue;
    do_print("Function resolved");
  } finally {
    // Local cleanup repair: the upstream queue success case leaked its handle.
    await openedFile.close();
  }
});

// Check if Scheduler.queue returned by OS.File.queue is resolved
// after an operation fails.
add_task(async function check_failure() {
  let exception;
  try {
    do_print("Attempting to open a non existing file");
    await OS.File.open(OS.Path.join(".", "Bigfoot"));
  } catch (err) {
    exception = err;
    await OS.File.queue;
  }
  Assert.ok(exception != null);
  do_print("Function resolved");
});
