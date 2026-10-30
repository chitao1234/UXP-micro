// |reftest| skip-if(!this.hasOwnProperty("Intl"))
// Any copyright is dedicated to the Public Domain.
// http://creativecommons.org/publicdomain/zero/1.0/

// Alphabetic variant replacements also match the language-subtag grammar.
// They must remain variants, including after sorting and duplicate removal.
const cases = {
    "el-polytoni": "el-polyton",
    "EL-GREK-GR-POLYTONI": "el-Grek-GR-polyton",
    "en-polytoni": "en-polyton",
    "el-fonipa-polytoni": "el-fonipa-polyton",
    "el-polytoni-fonipa": "el-fonipa-polyton",
    "el-polyton-polytoni": "el-polyton",
    "el-polytoni-polyton": "el-polyton",
    "el-heploc-polytoni": "el-alalc97-polyton",
    "el-polytoni-u-nu-latn-x-test": "el-polyton-u-nu-latn-x-test",
};

for (let [input, expected] of Object.entries(cases)) {
    assertEq(Intl.getCanonicalLocales(input)[0], expected);
    assertEq(Intl.getCanonicalLocales(expected)[0], expected);
    let locale = new Intl.Locale(input);
    assertEq(locale.toString(), expected);
    assertEq(locale.language, expected.split("-")[0]);
}

assertEqArray(Intl.getCanonicalLocales(["el-polytoni", "el-polyton"]), ["el-polyton"]);

if (typeof reportCompare === "function")
    reportCompare(true, true);
