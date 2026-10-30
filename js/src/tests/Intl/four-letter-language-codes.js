// |reftest| skip-if(!this.hasOwnProperty("Intl"))
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Unicode locale identifiers do not allow a four-letter language subtag.
// In particular, CLDR's "root" and script-only locale IDs are not language tags.
const languageTags = [
    "root", "Latn", "Flob", "ZORK", "Blah-latn", "QuuX-latn-us",
    "SPAM-gb-x-Sausages-BACON-eggs",
];

for (let tag of languageTags) {
    assertThrowsInstanceOf(() => Intl.getCanonicalLocales(tag), RangeError);
}

if (typeof reportCompare === "function")
    reportCompare(0, 0);
