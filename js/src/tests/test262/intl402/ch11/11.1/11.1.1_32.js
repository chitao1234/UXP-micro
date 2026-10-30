// Copyright 2013 Mozilla Corporation. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/**
 * @description Tests that the options minimumSignificantDigits and
 *     maximumSignificantDigits are read in the right sequence.
 * @author Norbert Lindenberg
 */

var read = 0;

function readMinimumSignificantDigits() {
    ++read;
    if (read !== 1)
        $ERROR("minimumSignificantDigits read out of sequence: " + read + ".");
    return 1;
}

function readMaximumSignificantDigits() {
    ++read;
    if (read !== 2)
        $ERROR("maximumSignificantDigits read out of sequence: " + read + ".");
    return 1;
}

var options = {};
Object.defineProperty(options, "minimumSignificantDigits",
    { get: readMinimumSignificantDigits });
Object.defineProperty(options, "maximumSignificantDigits",
    { get: readMaximumSignificantDigits });

new Intl.NumberFormat("de", options);

if (read !== 2) {
    $ERROR("insuffient number of property reads: " + read + ".");
}
