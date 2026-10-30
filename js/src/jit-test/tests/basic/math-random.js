// Xoroshiro128++ fixtures, calculated with uint64 rotation/masking.
function test() {
  setRNGState(0x12341234, 0x98765);

  function f() {
    let x = [];
    for (let i = 0; i < 10000; i++)  {
      x.push(Math.random());
    }
    return x;
  }
  let x = f();
  assertEq(x[0], 0.004453311485639322);
  assertEq(x[10], 0.9326664205537816);
  assertEq(x[100], 0.9421881704319355);
  assertEq(x[1000], 0.16279437623237047);
  assertEq(x[2000], 0.7009925267825348);
  assertEq(x[3000], 0.1602432569905028);
  assertEq(x[4000], 0.5997053836716703);
  assertEq(x[5000], 0.8835691072292435);
  assertEq(x[6000], 0.5008798652290354);
  assertEq(x[7000], 0.7811822251439959);
  assertEq(x[8000], 0.5104051231799199);
  assertEq(x[9000], 0.23257603598997056);
  assertEq(x[9999], 0.10083027458407512);

  // Test some other (arbitrary) seeds.
  setRNGState(0, 1);
  x = f();
  assertEq(x[0], 1.4551915228366852e-11);
  assertEq(x[2000], 0.7533580313725325);
  assertEq(x[6000], 0.16180187399380364);
  assertEq(x[9999], 0.6231433836651167);

  setRNGState(0x0fff0101, 0x44440001);
  x = f();
  assertEq(x[0], 0.02057174222866831);
  assertEq(x[2000], 0.5562893231955348);
  assertEq(x[6000], 0.9990420826431285);
  assertEq(x[9999], 0.0692805395017071);
}

if (typeof setRNGState == "function")
  test();
