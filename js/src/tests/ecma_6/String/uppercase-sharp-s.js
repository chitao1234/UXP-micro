// Default uppercase mapping follows Unicode SpecialCasing.txt, in both
// Latin-1 strings and strings requiring two-byte storage.
for (let text of ["\u00DF", "a\u00DFz", "\u0100\u00DF", "\u00DF\u0100"]) {
    assertEq(text.toUpperCase(), text.replace(/\u00DF/g, "SS").toUpperCase());
}
reportCompare(0, 0);
