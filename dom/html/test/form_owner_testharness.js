/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Native harness adapters for the four synchronous upstream form-owner tests.
// Keep their parser markup, operations and expected values unchanged.
function test(callback, description) {
  info(description);
  callback();
}
function assert_equals(actual, expected, description) {
  is(actual, expected, description);
}
function assert_array_equals(actual, expected, description) {
  is(actual.length, expected.length, description + " length");
  for (let i = 0; i < expected.length; ++i) {
    is(actual[i], expected[i], description + " [" + i + "]");
  }
}
