// |reftest| skip-if(!this.hasOwnProperty("Intl"))

// ECMA-402 includes legacy variant rules from CLDR's languageAlias and
// variantAlias data in supplementalMetadata.xml.
// <https://www.unicode.org/reports/tr35/#Legacy_Variants>

assertEq(Intl.getCanonicalLocales("sv-AALAND")[0], "sv-AX");
assertEq(Intl.getCanonicalLocales("no-BOKMAL")[0], "nb");
assertEq(Intl.getCanonicalLocales("no-NYNORSK")[0], "nn");
assertEq(Intl.getCanonicalLocales("en-POSIX")[0], "en-posix");
assertEq(Intl.getCanonicalLocales("el-POLYTONI")[0], "el-polyton");
assertEq(Intl.getCanonicalLocales("aa-SAAHO")[0], "ssy");

if (typeof reportCompare === "function")
    reportCompare(true, true);
