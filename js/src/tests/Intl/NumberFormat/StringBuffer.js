// |reftest| skip-if(!this.hasOwnProperty("Intl"))
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// The implementation of the format function uses the C++ StringBuffer class,
// which changes its storage model at 32 characters, and uses it in a way which
// also means that there's no room for null-termination at this limit.
// This test makes sure that none of this affects the output.

var format = new Intl.NumberFormat("it-IT", {minimumFractionDigits: 1});

const cases = [
    [1123123123123123123123.1, "1.123.123.123.123.123.100.000,0"],
    [12123123123123123123123.1, "12.123.123.123.123.122.000.000,0"],
    [123123123123123123123123.1, "123.123.123.123.123.120.000.000,0"],
];

for (let [number, expected] of cases) {
    assertEq(format.format(number), expected);

    // Independently reconstruct these large values from Number.toFixed's
    // exponential form. Keep exercising the buffer boundary without retaining
    // old ICU's loss of precision as the expected result.
    let parts = number.toFixed(format.resolvedOptions().maximumFractionDigits)
                      .match(/^(\d)\.(\d+)e\+(\d+)$/);
    assertEq(parts !== null, true);
    let digits = parts[1] + parts[2].padEnd(Number(parts[3]), "0");
    let groups = digits.match(/\d{1,3}(?=(?:\d{3})*$)/g);
    assertEq(groups.join(".") + ",0", expected);
}

reportCompare(0, 0, "ok");
