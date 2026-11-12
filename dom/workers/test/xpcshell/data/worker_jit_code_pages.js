/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Keep executable functions and IC-bearing objects across process-policy
// changes. Most functions become unreachable after each message so natural
// worker collections can retire their code while neighboring code survives.
var retained = [];
self.onmessage = function(event) {
  try {
    for (var entry of retained) {
      if (entry.fn(entry.object, 50) !== entry.expected)
        throw Error('retained code or IC result changed');
    }
    var round = event.data;
    var functions = [];
    for (var i = 0; i < 64; i++) {
      var bias = round * 1000 + i;
      var fn = new Function('object', 'count',
        'var sum=0;for(var j=0;j<count;j++)sum+=object.x+j;return sum+' + bias);
      var object = {x: 3};
      for (var hot = 0; hot < 48; hot++) {
        if (fn(object, 50) !== 1375 + bias)
          throw Error('fresh code result changed');
      }
      functions.push({fn, object, expected: 1375 + bias});
    }
    retained.push(functions[round % functions.length]);
    self.postMessage({round, retained: retained.length, calls: 64 * 48});
  } catch (error) {
    self.postMessage({error: String(error), stack: error.stack});
  }
};
