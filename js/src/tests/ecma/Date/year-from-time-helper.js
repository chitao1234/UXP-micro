// The Date test oracle must handle negative years and Gregorian boundaries
// without using the Date implementation whose results it is checking.
assertEq(YearFromTime(0), 1970);
assertEq(YearFromTime(-1), 1969);
assertEq(YearFromTime(TIME_0000), 0);
assertEq(YearFromTime(NaN), NaN);
assertEq(YearFromTime(Infinity), NaN);
assertEq(YearFromTime(-Infinity), NaN);

function checkYear(year) {
  var start = TimeFromYear(year);
  var end = TimeFromYear(year + 1);
  assertEq(YearFromTime(start), year);
  assertEq(YearFromTime(start - 1), year - 1);
  assertEq(YearFromTime(end - 1), year);
  assertEq(YearFromTime(end), year + 1);
  assertEq(YearFromTime((start + end) / 2), year);
}

// Four whole calendar cycles cover leap days, century exceptions, year zero
// and negative years. Include the extremes of the ECMAScript Date range.
for (var year = -800; year <= 800; ++year)
  checkYear(year);
for (var year of [-271822, -271821, -10000, 1600, 1900, 1970, 2000, 2100,
                 2400, 10000, 275760, 275761])
  checkYear(year);

if (typeof reportCompare === "function")
  reportCompare(true, true);
