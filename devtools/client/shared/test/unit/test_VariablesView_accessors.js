/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { classes: Cc, interfaces: Ci, utils: Cu } = Components;
const { VariablesView } = Cu.import(
  "resource://devtools/client/shared/widgets/VariablesView.jsm", {});

function run_test() {
  const functionGrip = { type: "object", class: "Function" };
  const undefinedGrip = { type: "undefined" };
  const accessors = [
    { get: functionGrip },
    { set: functionGrip },
    { get: functionGrip, set: functionGrip },
    { get: functionGrip, set: undefinedGrip },
    { get: undefinedGrip, set: functionGrip }
  ];

  const parser = Cc["@mozilla.org/xmlextras/domparser;1"].createInstance(Ci.nsIDOMParser);
  const doc = parser.parseFromString("<div></div>", "text/html");
  const view = new VariablesView(doc.body.firstChild);
  const scope = view.addScope("accessors");

  for (let i = 0; i < accessors.length; i++) {
    const descriptor = accessors[i];
    equal(VariablesView.isPrimitive(descriptor), false,
          "Accessor descriptors are not primitive data descriptors");
    const item = scope.addItem("accessor" + i, descriptor);
    equal(item.twisty, true, "The accessor retains an expansion arrow");
    equal(item.expanded, true, "Getter and setter entries are initially expanded");
    ok(item.get("get"), "The getter entry is inspectable");
    ok(item.get("set"), "The setter entry is inspectable");
    item.collapse();
    equal(item.expanded, false, "The accessor can be collapsed");
    item.expand();
    equal(item.expanded, true, "The accessor can be expanded again");
  }

  equal(VariablesView.isPrimitive({ value: undefinedGrip }), true,
        "Undefined data values remain primitive");
  equal(VariablesView.isPrimitive({ value: { type: "BigInt", text: "123" } }), true,
        "BigInt data values remain primitive");
  equal(VariablesView.getString({ type: "BigInt", text: "123" }), "123n",
        "BigInt display remains supported");
}
