// |reftest| skip-if(!this.hasOwnProperty("Intl")||!this.hasOwnProperty("addIntlExtras"))

addIntlExtras(Intl);

function IsConstructor(o) {
  try {
    new (new Proxy(o, {construct: () => ({})}));
    return true;
  } catch (e) {
    return false;
  }
}

function thisValues() {
    const intlConstructors = Object.getOwnPropertyNames(Intl).map(name => Intl[name]).filter(IsConstructor);

    return [
        // Primitive values.
        ...[undefined, null, true, "abc", Symbol(), 123],

        // Object values.
        ...[{}, [], /(?:)/, function(){}, new Proxy({}, {})],

        // Intl objects.
        ...[].concat(...intlConstructors.map(ctor => [
            // Instance of an Intl constructor.
            new ctor("en"),

            // Instance of a subclassed Intl constructor.
            new (class extends ctor {})("en"),

            // Object inheriting from an Intl constructor prototype.
            Object.create(ctor.prototype),

            // Intl object not inheriting from its default prototype.
            Object.setPrototypeOf(new ctor("en"), Object.prototype),
        ])),
    ];
}

// Intl.PluralRules requires NewTarget, including when a receiver is supplied.
assertThrowsInstanceOf(() => Intl.PluralRules(), TypeError);
for (let thisValue of thisValues()) {
    assertThrowsInstanceOf(() => Intl.PluralRules.call(thisValue), TypeError);
}

// Fail before inspecting the receiver, locales or options.
let poison = new Proxy({}, {get() { throw new Error("unexpected access"); },
                            isExtensible() { throw new Error("unexpected access"); }});
assertThrowsInstanceOf(() => Intl.PluralRules.call(poison, poison, poison), TypeError);
assertThrowsInstanceOf(() => Reflect.apply(Intl.PluralRules, null, [poison, poison]), TypeError);
assertThrowsInstanceOf(() => Intl.PluralRules.bind(poison, poison, poison)(), TypeError);

if (typeof reportCompare === "function")
    reportCompare(true, true);
