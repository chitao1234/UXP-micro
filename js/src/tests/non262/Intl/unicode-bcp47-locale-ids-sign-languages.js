// |reftest| skip-if(!this.hasOwnProperty("Intl"))

// The IANA language subtag registry contains replacements for sign language
// codes marked as redundant. For example:
//
//   Type: redundant
//   Tag: sgn-DE
//   Description: German Sign Language
//   Added: 2001-11-11
//   Deprecated: 2009-07-29
//   Preferred-Value: gsg
//
// CLDR 48 includes these multi-subtag language aliases too.

assertEq(Intl.getCanonicalLocales("sgn-DE")[0], "gsg");
assertEq(Intl.getCanonicalLocales("sgn-Latn-DE")[0], "gsg-Latn");
assertEq(Intl.getCanonicalLocales("sgn-AQ")[0], "sgn-AQ");

if (typeof reportCompare === "function")
    reportCompare(true, true);
