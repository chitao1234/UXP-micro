// |reftest| skip-if(!this.hasOwnProperty("Intl"))
// Any copyright is dedicated to the Public Domain.
// http://creativecommons.org/publicdomain/zero/1.0/

class MyPluralRules extends Intl.PluralRules {}
let rules = new MyPluralRules("en");
assertEq(Object.getPrototypeOf(rules), MyPluralRules.prototype);
assertEq(rules instanceof MyPluralRules, true);
assertEq(rules.select(1), "one");
assertEq(rules.select(2), "other");

let select = Intl.PluralRules.prototype.select;
let resolvedOptions = Intl.PluralRules.prototype.resolvedOptions;
for (let proto of [null, undefined, 42, "prototype", {}, Array.prototype]) {
    function NewTarget() {}
    NewTarget.prototype = proto;
    rules = Reflect.construct(Intl.PluralRules, ["en"], NewTarget);
    assertEq(Object.getPrototypeOf(rules),
             typeof proto === "object" && proto !== null ? proto : Intl.PluralRules.prototype);
    assertEq(select.call(rules, 1), "one");
    assertEq(resolvedOptions.call(rules).type, "cardinal");
}

// Read NewTarget.prototype exactly once, before observing the locales/options.
let seen = [];
let customProto = {};
let newTarget = new Proxy(function() {}, {get(target, key) {
    seen.push(key);
    if (key === "prototype")
        return customProto;
    throw new Error("unexpected newTarget access");
}});
let options = {get type() { seen.push("type"); return "ordinal"; }};
rules = Reflect.construct(Intl.PluralRules, ["en", options], newTarget);
assertEq(Object.getPrototypeOf(rules), customProto);
assertEqArray(seen, ["prototype", "type"]);
assertEq(select.call(rules, 2), "two");

let sentinel = {};
let throwingTarget = new Proxy(function() {}, {get() { throw sentinel; }});
let poison = new Proxy({}, {get() { throw new Error("unexpected argument access"); }});
let caught;
try { Reflect.construct(Intl.PluralRules, [poison, poison], throwingTarget); }
catch (e) { caught = e; }
assertEq(caught, sentinel);

let Bound = Intl.PluralRules.bind(null, "en");
assertEq(new Bound().select(1), "one");
assertThrowsInstanceOf(() => select.call(Object.create(Intl.PluralRules.prototype), 1), TypeError);
assertThrowsInstanceOf(() => new Intl.PluralRules("en", {type: "invalid"}), RangeError);

if (typeof newGlobal === "function") {
    let other = newGlobal();
    rules = Reflect.construct(Intl.PluralRules, ["en"], other.Intl.PluralRules);
    assertEq(Object.getPrototypeOf(rules), other.Intl.PluralRules.prototype);
    assertEq(select.call(rules, 1), "one");
}

if (typeof reportCompare === "function")
    reportCompare(true, true);
