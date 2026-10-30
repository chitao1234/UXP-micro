/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const { deriveHawkCredentials } =
  Cu.import("resource://services-common/hawkrequest.js", {});

// RFC 5869 / SHA-256 with the FxA onepw KW namespace, no salt, and token bytes
// a0..bf. The sessionToken ID/key also match the published onepw test vector;
// all 96 output bytes were cross-checked with Python hashlib/hmac.
const TOKEN = "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf" +
              "b0b1b2b3b4b5b6b7b8b9babbbcbdbebf";
const VECTORS = [
  ["sessionToken",
   "c0a29dcf46174973da1378696e4c82ae10f723cf4f4d9f75e39f4ae3851595ab" +
   "9d8f22998ee7f5798b887042466b72d53e56ab0c094388bf65831f702d2febc0" +
   "37e3fceb754cb57362a8bd60a2f2344a7c285d17ddb2b57733640ce6d1b6a7a8"],
  ["keyFetchToken",
   "70db599cec9c040b10c790418f93fe77711fdea352a59e9b02d2336136d39f68f9" +
   "36647aab7765642f3ee1c704751e501f50f8188ec55c31df4dbfc28f16816fd2" +
   "7327daae0c97e2b785eeecd78b69ddda0ea5f8acc9758d49f3afc5d4ca6101"],
  ["\u00e9",
   "9a2151ce62f00816bad8d89f48b04c24eaeacfce8bd38922374ec7b059dd59d344" +
   "bcd67199f48678a4101f8ca43723ce373e4f8e5de9cd8daf8bddf57628fb170" +
   "1e483832fe17f477b9f98b201115fea55b956d68076c4fbccdbccc01be62864"],
];

function run_test() {
  for (const [context, expected] of VECTORS) {
    const credentials = deriveHawkCredentials(TOKEN, context);
    equal(credentials.algorithm, "sha256", context + " uses SHA-256");
    equal(credentials.id, expected.slice(0, 64), context + " derives its ID");
    equal(CommonUtils.bytesAsHex(credentials.key), expected.slice(64, 128),
          context + " derives its binary signing key");
    equal(CommonUtils.bytesAsHex(credentials.extra), expected.slice(128),
          context + " retains the remaining key material");

    const short = deriveHawkCredentials(TOKEN, context, 64, true);
    equal(short.id, credentials.id, context + " ID is independent of length");
    equal(short.key, expected.slice(64, 128), context + " supports hex keys");
    ok(!("extra" in short), context + " omits extra for 64 output bytes");

    const extended = deriveHawkCredentials(TOKEN, context, 80);
    equal(CommonUtils.bytesAsHex(extended.extra), expected.slice(128, 160),
          context + " honors the requested extra length");
  }
}
