/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { TabSources } = require("devtools/server/actors/utils/TabSources");

function run_test() {
  // Source policy must not inspect text to identify an engine intrinsic.
  // Neither the old nor revised internal prototype spelling identifies an
  // author source, and loading text can itself have observable cost or fail.
  const sources = new TabSources({});
  for (const text of ["() {\n}", "function () {\n}"]) {
    do_check_true(sources.allowSource({ text }));
  }
  const unavailableText = Object.defineProperty({}, "text", {
    get() { throw new Error("Source policy must not request source text"); }
  });
  do_check_true(sources.allowSource(unavailableText));
  const rejected = {};
  const filtered = new TabSources({}, source => source !== rejected);
  do_check_false(filtered.allowSource(rejected));
  do_check_true(filtered.allowSource(unavailableText));

  do_test_pending();
  Task.spawn(function* () {
    initTestDebuggerServer();
    const debuggee = addTestGlobal("native-function-prototype");
    const client = new DebuggerClient(DebuggerServer.connectPipe());
    yield client.connect();
    const [,, thread] = yield attachTestTabAndResume(client, "native-function-prototype");

    let response = yield getSources(thread);
    do_check_eq(response.sources.length, 0);

    const examples = [
      { url: "http://example.com/anonymous-author.js", text: "(function () {\n})" },
      { url: "http://example.com/named-author.js", text: "function author() {\n}" },
    ];
    for (let i = 0; i < examples.length; i++) {
      const example = examples[i];
      // Keep the anonymous function alive while asynchronous source requests
      // run, so GC cannot remove its otherwise unreferenced script.
      debuggee["authorSource" + i] =
        Cu.evalInSandbox(example.text, debuggee, "1.8", example.url, 1);
    }

    response = yield getSources(thread);
    do_check_eq(response.sources.length, examples.length);
    for (const example of examples) {
      const form = response.sources.filter(source => source.url === example.url)[0];
      do_check_true(!!form);
      const content = yield thread.source(form).source();
      do_check_true(!content.error);
      do_check_eq(content.source, example.text);
    }
    yield client.close();
    do_test_finished();
  }).then(null, do_throw);
}
