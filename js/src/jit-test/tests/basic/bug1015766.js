gczeal(8, 2)
try {
    [new String, y]
} catch (e) {}
r = /()/
try {
    "".replace(r, () => {
	[]()
    });
} catch(e) {}

// Result construction can compact RegExpShared as it allocates strings and
// arrays. Named groups and indices also allocate before reusing the template.
var named = /(?<name>x)/d;
for (var i = 0; i < 8; i++) {
    var match = named.exec("x");
    assertEq(match[0], "x");
    assertEq(match.groups.name, "x");
    assertEq(match.indices.groups.name[0], 0);
    assertEq(match.indices.groups.name[1], 1);
}
