// |reftest| skip-if(!this.hasOwnProperty("Intl")||!this.hasOwnProperty("addIntlExtras"))
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests the format function with a diverse set of locales and options.
// Always use UTC to avoid dependencies on test environment.

let mozIntl = {};
addIntlExtras(mozIntl);

// Pattern

var dtf = new Intl.DateTimeFormat("en-US", {pattern: "HH:mm MM/dd/YYYY", timeZone: "UTC"});
var mozDtf = new mozIntl.DateTimeFormat("en-US", {pattern: "HH:mm MM/dd/YYYY", timeZone: "UTC"});

assertEq(dtf.resolvedOptions().hasOwnProperty('pattern'), false);
assertEq(mozDtf.resolvedOptions().pattern, "HH:mm MM/dd/YYYY");

const date = new Date(Date.UTC(2020, 5, 17, 13, 14, 15));
assertEq(mozDtf.format(date), "13:14 06/17/2020");
assertEq(dtf.format(date), new Intl.DateTimeFormat("en-US", {timeZone: "UTC"}).format(date));

// Styles are standard options now. Both constructors report the requested
// styles, without exposing the individual fields chosen by ICU's pattern.
for (let options of [{dateStyle: "long"}, {timeStyle: "long"},
                     {dateStyle: "medium", timeStyle: "medium"}]) {
    options.timeZone = "UTC";
    let standard = new Intl.DateTimeFormat("en-US", options);
    let extended = new mozIntl.DateTimeFormat("en-US", options);
    assertEq(standard.format(date), extended.format(date));
    for (let formatter of [standard, extended]) {
        let resolved = formatter.resolvedOptions();
        for (let style of ["dateStyle", "timeStyle"]) {
            assertEq(resolved.hasOwnProperty(style), options.hasOwnProperty(style));
            assertEq(resolved[style], options[style]);
        }
        for (let field of ["year", "month", "day", "hour", "minute", "second"])
            assertEq(resolved.hasOwnProperty(field), false);
    }
}

for (let Constructor of [Intl.DateTimeFormat, mozIntl.DateTimeFormat]) {
    assertThrowsInstanceOf(() => new Constructor("en-US", {dateStyle: "long", year: "numeric"}), TypeError);
    assertThrowsInstanceOf(() => new Constructor("en-US", {timeStyle: "long", hour: "numeric"}), TypeError);
}

reportCompare(0, 0, 'ok');
