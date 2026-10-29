// Copyright (C) 2016 Jordan Harband. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
description: Object.getOwnPropertyDescriptors propagates duplicate ownKeys errors
esid: pending
author: Jordan Harband
features: [Proxy]
---*/

var descriptorCalls = 0;
var log = '';
var proxy = new Proxy({}, {
  ownKeys() {
    log += 'ownKeys|';
    return ['DUPLICATE', 'DUPLICATE', 'DUPLICATE'];
  },
  getOwnPropertyDescriptor(t, name) {
    log += 'getOwnPropertyDescriptor:' + name + '|';
    descriptorCalls++;
    return { configurable: true, enumerable: true, value: 1, writable: true };
  }
});

assert.throws(TypeError, function() {
  Object.getOwnPropertyDescriptors(proxy);
});
assert.sameValue(descriptorCalls, 0);
assert.sameValue(log, 'ownKeys|');
