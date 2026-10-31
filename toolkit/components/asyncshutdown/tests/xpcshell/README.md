# AsyncShutdown regression coverage

Restored from Firefox 78 revision
`600df6aa643726626d2eab36bc0f6e34bbe5ac2a`, under the same directory.
UXP `0ddd00f195` deleted this suite in 2020 while retaining the APIs.

All upstream assertions remain. Harness adaptations use `Components.utils.import`,
explicit component aliases, `do_print`, and the third `notifyObservers` argument. Cleanup restores
the testing preference even when a test fails. The upstream uncaught-rejection
test retains its explicit PromiseTestUtils opt-out because rejected blockers
and throwing state callbacks are its subject.

Two labeled local oracle repairs also fix defects present in the inspected
current Firefox revision `309a5a01511293cd91f86d82c9c2f7130dd38ebc`:

- Match the unwrapped barrier kind explicitly instead of testing a truthy string.
- Track all ten pending conditions before asserting their eventual completion;
  upstream's two `every` assertions otherwise inspected an empty array.

Original Firefox 78 SHA256 values:

| File | SHA256 |
| --- | --- |
| head.js | 0fedd9993f68eca19f3c8ec19fd7b4083ffacf4c9fe1b2e05561000ff5c2c876 |
| test_AsyncShutdown.js | c373e9152376f4f203067354ac98432613706d40496d59918242e2fe286f936b |
| test_AsyncShutdown_leave_uncaught.js | 3687d28e2e29953ea5b94c217a8da713049fb12dfeedca315296db6aca3173a6 |
| test_converters.js | 28adb3aac9eec9f3107b49dc82788b9a891bd152250cf0ebb4cba5e56e95f867 |
| xpcshell.ini | 6e41e16623d68f7c5eaab042abbd953e442cedf94535267a42f1b18d639627fd |
