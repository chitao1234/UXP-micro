load(libdir + "asserts.js");

function checkMethods(proto) {
    var names = Object.getOwnPropertyNames(proto);
    for (var i = 0; i < names.length; i++) {
        var name = names[i];
        if (name === "caller" || name === "arguments") {
            assertThrowsInstanceOf(() => proto[name], TypeError);
        } else {
            var prop = proto[name];
        }
    }
}
checkMethods(Function.prototype);
