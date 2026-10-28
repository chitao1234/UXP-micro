// |jit-test|
// Test all runtime i64 division/remainder result words against an independent
// BigInt oracle. This runs under the compiler and module policy selected by
// the harness; it does not rely on privileged embedding APIs or test shims.
load(libdir + "wasm.js");

(function() {
    var shift = BigInt(32);
    var wordBase = BigInt(4294967296);
    var wordMask = wordBase - BigInt(1);
    var fullBase = wordBase * wordBase;
    var signBit = fullBase / BigInt(2);
    var operations = [
        {name: "sdiv", opcode: "i64.div_s", signed: true, remainder: false},
        {name: "srem", opcode: "i64.rem_s", signed: true, remainder: true},
        {name: "udiv", opcode: "i64.div_u", signed: false, remainder: false},
        {name: "urem", opcode: "i64.rem_u", signed: false, remainder: true},
    ];
    var functions = [];
    for (var operation of operations) {
        var left = "(i64.or (i64.shl (i64.extend_u/i32 (get_local 0)) (i64.const 32)) " +
                   "(i64.extend_u/i32 (get_local 1)))";
        var right = "(i64.or (i64.shl (i64.extend_u/i32 (get_local 2)) (i64.const 32)) " +
                    "(i64.extend_u/i32 (get_local 3)))";
        var expression = "(" + operation.opcode + " " + left + " " + right + ")";
        for (var high of [false, true]) {
            var value = high ? "(i64.shr_u " + expression + " (i64.const 32))" : expression;
            functions.push("(func (export \"" + operation.name + (high ? "Hi" : "Lo") + "\") " +
                "(param i32 i32 i32 i32) (result i32) (i32.wrap/i64 " + value + "))");
        }
    }
    var module = new WebAssembly.Module(wasmTextToBinary("(module " + functions.join(" ") + ")"));
    var exports = new WebAssembly.Instance(module).exports;
    function bits(value) {
        value %= fullBase;
        return value < BigInt(0) ? value + fullBase : value;
    }
    function signed(value) {
        return value >= signBit ? value - fullBase : value;
    }
    function pair(numerator, divisor) {
        numerator = bits(numerator);
        divisor = bits(divisor);
        var words = [Number((numerator >> shift) & wordMask) | 0,
                     Number(numerator & wordMask) | 0,
                     Number((divisor >> shift) & wordMask) | 0,
                     Number(divisor & wordMask) | 0];
        for (var operation of operations) {
            var left = operation.signed ? signed(numerator) : numerator;
            var right = operation.signed ? signed(divisor) : divisor;
            var traps = right === BigInt(0) ||
                (operation.signed && !operation.remainder && left === -signBit && right === BigInt(-1));
            var expected = traps ? null : bits(operation.remainder ? left % right : left / right);
            for (var high of [false, true]) {
                var name = operation.name + (high ? "Hi" : "Lo");
                var label = name + "(" + words.join(",") + ")";
                if (traps) {
                    var exception = null;
                    try { exports[name].apply(null, words); }
                    catch (error) { exception = error; }
                    assertEq(exception instanceof WebAssembly.RuntimeError, true, label);
                } else {
                    var word = high ? Number((expected >> shift) & wordMask) | 0
                                    : Number(expected & wordMask) | 0;
                    assertEq(exports[name].apply(null, words), word, label);
                }
            }
        }
    }
    function fromWords(hi, lo) {
        return BigInt(hi >>> 0) * wordBase + BigInt(lo >>> 0);
    }

    // Signed/unsigned extrema and both sides of the 32-bit word boundary.
    var edges = [BigInt(0), BigInt(1), BigInt(3), BigInt(7),
                 wordBase / BigInt(2), wordBase - BigInt(1), wordBase, wordBase + BigInt(1),
                 signBit - BigInt(1), signBit, fullBase - BigInt(2), fullBase - BigInt(1)];
    for (var numerator of edges) {
        for (var divisor of edges) pair(numerator, divisor);
    }
    // -2^31/-1 must not become a hardware idiv overflow: the i64 quotient fits.
    pair(-wordBase / BigInt(2), BigInt(-1));
    // Signed64 minimum/-1 traps for division, but remainder is exactly zero.
    pair(-signBit, BigInt(-1));

    // Near exact multiples exercise normalization shifts and downward quotient
    // corrections, including divisors with low word zero and the top bit set.
    for (var hi of [1, 0x7fffffff, 0x80000000, 0xffffffff]) {
        for (var lo of [0, 1, 0xffffffff]) {
            var d = fromWords(hi, lo);
            for (var q of [1, 2, 0xffffffff]) {
                for (var delta of [BigInt(-1), BigInt(0), BigInt(1), d - BigInt(1)]) {
                    var n = d * BigInt(q) + delta;
                    if (n < BigInt(0) || n >= fullBase) continue;
                    pair(n, d);
                    pair(-n, d);
                    pair(n, -d);
                    pair(-n, -d);
                }
            }
        }
    }

    // Deterministic full-width and skewed-width inputs exercise cases that
    // ordinary uniformly random64/64 inputs (mostly quotients0/1) miss.
    var seed = 0x6a09e667;
    function randomWord() {
        seed ^= seed << 13;
        seed ^= seed >>> 17;
        seed ^= seed << 5;
        return seed | 0;
    }
    for (var i = 0; i < 32; i++) {
        pair(fromWords(randomWord(), randomWord()), fromWords(randomWord(), randomWord()));
        pair(fromWords(randomWord(), randomWord()), fromWords(0, randomWord()));
        var lowDivisor = randomWord();
        pair(fromWords(randomWord(), randomWord()), fromWords(lowDivisor >> 31, lowDivisor));
        var highDivisor = ((randomWord() >>> i) | 1) >>> 0;
        pair(fromWords(randomWord(), randomWord()), fromWords(highDivisor, randomWord()));
    }
})();
