/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "CSSCalc.h"
#include <initializer_list>
#include <limits>

using namespace mozilla::css;

TEST(CSSCalc, ComparisonSpecialValues)
{
  BasicFloatCalcOps ops;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  for (float value : { -1.0f, -0.0f, 0.0f, 1.0f, nan }) {
    EXPECT_TRUE(std::isnan(ops.MergeMinMax(eCSSUnit_Calc_Min, value, nan)));
    EXPECT_TRUE(std::isnan(ops.MergeMinMax(eCSSUnit_Calc_Min, nan, value)));
    EXPECT_TRUE(std::isnan(ops.MergeMinMax(eCSSUnit_Calc_Max, value, nan)));
    EXPECT_TRUE(std::isnan(ops.MergeMinMax(eCSSUnit_Calc_Max, nan, value)));
    EXPECT_TRUE(std::isnan(ops.MergeClamp(nan, value, 1.0f)));
    EXPECT_TRUE(std::isnan(ops.MergeClamp(0.0f, nan, value)));
    EXPECT_TRUE(std::isnan(ops.MergeClamp(0.0f, value, nan)));
  }
  EXPECT_TRUE(std::signbit(CalcMin(0.0f, -0.0f)));
  EXPECT_TRUE(std::signbit(CalcMin(-0.0f, 0.0f)));
  EXPECT_FALSE(std::signbit(CalcMax(0.0f, -0.0f)));
  EXPECT_FALSE(std::signbit(CalcMax(-0.0f, 0.0f)));
  EXPECT_FALSE(std::signbit(ops.MergeClamp(0.0f, -0.0f, -0.0f)));
  EXPECT_TRUE(std::signbit(ops.MergeClamp(-0.0f, -0.0f, 0.0f)));
  EXPECT_EQ(10.0f, ops.MergeClamp(10.0f, 5.0f, 0.0f));
}
