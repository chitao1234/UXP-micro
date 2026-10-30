// |reftest| skip-if(!this.hasOwnProperty("Intl"))
// Any copyright is dedicated to the Public Domain.
// http://creativecommons.org/publicdomain/zero/1.0/

// CLDR 48 languageAlias patterns match a subset of fields and variants.
// Specific rules precede wildcard und rules; unmatched fields survive.
const cases = {
    "sv-aaland": "sv-AX",
    "sv-Latn-aaland": "sv-Latn-AX",
    "sv-SE-aaland": "sv-SE",
    "hy-arevela": "hy",
    "hy-Armn-AM-arevela-fonipa": "hy-Armn-AM-fonipa",
    "hy-arevmda": "hyw",
    "hy-Armn-AM-arevmda": "hyw-Armn-AM",
    "en-arevmda": "en",
    "no-bokmal": "nb",
    "no-nynorsk": "nn",
    "en-nynorsk": "en",
    "aa-saaho": "ssy",
    "en-saaho": "en",
    "ja-Latn-hepburn-heploc": "ja-Latn-alalc97",
    "ja-Latn-heploc-hepburn-fonipa": "ja-Latn-alalc97-fonipa",
    "ja-Latn-alalc97-hepburn-heploc": "ja-Latn-alalc97",
    "ja-Latn-aaland-hepburn-heploc": "ja-Latn-AX-alalc97",
    "sgn-US": "ase",
    "sgn-Latn-US-fonipa": "ase-Latn-fonipa",
    "sgn-BR": "bzs",
    "sgn-ZA": "sfs",
    "art-Latn-lojban": "jbo-Latn",
    "cel-Latn-FR-gaulish-fonipa": "xtg-Latn-FR-fonipa",
    "zh-Hant-hakka": "hak-Hant",
    "zh-CN-xiang": "hsn-CN",
    "en-hepburn": "en-hepburn",
};

for (let [input, expected] of Object.entries(cases)) {
    assertEq(Intl.getCanonicalLocales(input)[0], expected);
    assertEq(Intl.getCanonicalLocales(expected)[0], expected);
    assertEq(new Intl.Locale(input).toString(), expected);
    for (let extra of ["-u-nu-latn", "-x-test", "-u-nu-latn-x-test"]) {
        assertEq(Intl.getCanonicalLocales(input + extra)[0], expected + extra);
        assertEq(new Intl.Locale(input + extra).toString(), expected + extra);
    }
}

assertEqArray(Intl.getCanonicalLocales(["sv-aaland", "sv-AX"]), ["sv-AX"]);
assertEq(Intl.getCanonicalLocales("en-t-hy-arevmda")[0], "en-t-hyw");
assertEq(new Intl.Locale("sv-aaland", {region: "FI"}).toString(), "sv-FI");
assertThrowsInstanceOf(() => Intl.getCanonicalLocales("hy-arevela-arevela"), RangeError);

if (typeof oomTest === "function") {
    oomTest(() => Intl.getCanonicalLocales("ja-Latn-hepburn-heploc-fonipa-u-nu-latn"));
    assertEq(Intl.getCanonicalLocales("ja-Latn-hepburn-heploc")[0], "ja-Latn-alalc97");
}

// Placeholder-language results exercise prefixing "und" to an ICU locale ID.
for (let tag of ["und-Cpmn", "und-Cpmn-CY", "und-Qaaa-ZZ"]) {
    let maximum = new Intl.Locale(tag).maximize();
    assertEq(maximum.maximize().toString(), maximum.toString());
    assertEq(maximum.minimize().maximize().toString(), maximum.toString());
}

if (typeof reportCompare === "function")
    reportCompare(true, true);
