// From Firefox78 pointerevent_support.js; used by the multitype release test.
var globalPointerEventTest = null;
var expectedPointerType = null;
const ALL_POINTERS = ["mouse", "touch", "pen"];
const HOVERABLE_POINTERS = ["mouse", "pen"];
const NOHOVER_POINTERS = ["touch"];

function MultiPointerTypeTest(testName, types) {
  this.testName = testName;
  this.types = types;
  this.currentTypeIndex = 0;
  this.currentTest = null;
  this.createNextTest();
}

MultiPointerTypeTest.prototype.skip = function() {
  var prevTest = this.currentTest;
  this.createNextTest();
  prevTest.timeout();
};

MultiPointerTypeTest.prototype.done = function() {
  var prevTest = this.currentTest;
  this.createNextTest();
  if (prevTest != null) {
    prevTest.done();
  }
};

MultiPointerTypeTest.prototype.step = function(stepFunction) {
  this.currentTest.step(stepFunction);
};

MultiPointerTypeTest.prototype.createNextTest = function() {
  if (this.currentTypeIndex < this.types.length) {
    var pointerTypeDescription = document.getElementById(
      "pointerTypeDescription"
    );
    document.getElementById("pointerTypeDescription").innerHTML =
      "Follow the test instructions with <span style='color: red'>" +
      this.types[this.currentTypeIndex] +
      "</span>. If you don't have the device <a href='javascript:;' onclick='globalPointerEventTest.skip()'>skip it</a>.";
    this.currentTest = async_test(
      this.types[this.currentTypeIndex] + " " + this.testName
    );
    expectedPointerType = this.types[this.currentTypeIndex];
    this.currentTypeIndex++;
  } else {
    document.getElementById("pointerTypeDescription").innerHTML = "";
  }
  resetTestState();
};

function setup_pointerevent_test(testName, supportedPointerTypes) {
  return (globalPointerEventTest = new MultiPointerTypeTest(
    testName,
    supportedPointerTypes
  ));
}
