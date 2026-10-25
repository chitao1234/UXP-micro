/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"

#include "mozilla/CSSVariableValues.h"

using namespace mozilla;

TEST(CSSVariableValues, PutGetAndOrder)
{
  CSSVariableValues values;
  values.Put(NS_LITERAL_STRING("b"), NS_LITERAL_STRING("2"),
             eCSSTokenSerialization_Nothing,
             eCSSTokenSerialization_Nothing);
  values.Put(NS_LITERAL_STRING("a"), NS_LITERAL_STRING("1"),
             eCSSTokenSerialization_Nothing,
             eCSSTokenSerialization_Nothing);

  nsString value;
  ASSERT_TRUE(values.Get(NS_LITERAL_STRING("a"), value));
  EXPECT_TRUE(value.EqualsLiteral("1"));
  ASSERT_EQ(2u, values.Count());

  nsString name;
  values.GetVariableAt(0, name);
  EXPECT_TRUE(name.EqualsLiteral("b"));
  values.GetVariableAt(1, name);
  EXPECT_TRUE(name.EqualsLiteral("a"));
}

TEST(CSSVariableValues, CopyOnWriteAfterInterning)
{
  CSSVariableValues original;
  original.Put(NS_LITERAL_STRING("x"), NS_LITERAL_STRING("old"),
              eCSSTokenSerialization_Nothing,
              eCSSTokenSerialization_Nothing);
  original.Intern();

  CSSVariableValues copy(original);
  copy.Put(NS_LITERAL_STRING("x"), NS_LITERAL_STRING("new"),
           eCSSTokenSerialization_Nothing,
           eCSSTokenSerialization_Nothing);

  nsString value;
  ASSERT_TRUE(original.Get(NS_LITERAL_STRING("x"), value));
  EXPECT_TRUE(value.EqualsLiteral("old"));
  ASSERT_TRUE(copy.Get(NS_LITERAL_STRING("x"), value));
  EXPECT_TRUE(value.EqualsLiteral("new"));
}

TEST(CSSVariableValues, InternThenMutateInPlace)
{
  CSSVariableValues values;
  values.Put(NS_LITERAL_STRING("x"), NS_LITERAL_STRING("old"),
             eCSSTokenSerialization_Nothing,
             eCSSTokenSerialization_Nothing);
  values.Intern();

  // The cached block is now the sole owner.  This must detach before the
  // mutation or the cache would retain a block whose hash no longer matches.
  values.Put(NS_LITERAL_STRING("x"), NS_LITERAL_STRING("new"),
             eCSSTokenSerialization_Nothing,
             eCSSTokenSerialization_Nothing);

  nsString value;
  ASSERT_TRUE(values.Get(NS_LITERAL_STRING("x"), value));
  EXPECT_TRUE(value.EqualsLiteral("new"));
  values.Intern();
  ASSERT_TRUE(values.Get(NS_LITERAL_STRING("x"), value));
  EXPECT_TRUE(value.EqualsLiteral("new"));
}

TEST(CSSVariableValues, RemoveAndReadd)
{
  CSSVariableValues values;
  values.Put(NS_LITERAL_STRING("a"), NS_LITERAL_STRING("1"),
             eCSSTokenSerialization_Nothing,
             eCSSTokenSerialization_Nothing);
  values.Put(NS_LITERAL_STRING("b"), NS_LITERAL_STRING("2"),
             eCSSTokenSerialization_Nothing,
             eCSSTokenSerialization_Nothing);
  values.Intern();

  values.Remove(NS_LITERAL_STRING("a"));
  EXPECT_EQ(1u, values.Count());

  nsString value;
  EXPECT_FALSE(values.Get(NS_LITERAL_STRING("a"), value));
  ASSERT_TRUE(values.Get(NS_LITERAL_STRING("b"), value));
  EXPECT_TRUE(value.EqualsLiteral("2"));

  values.Put(NS_LITERAL_STRING("a"), NS_LITERAL_STRING("3"),
             eCSSTokenSerialization_Nothing,
             eCSSTokenSerialization_Nothing);
  ASSERT_TRUE(values.Get(NS_LITERAL_STRING("a"), value));
  EXPECT_TRUE(value.EqualsLiteral("3"));
}

TEST(CSSVariableValues, EqualsInOrder)
{
  CSSVariableValues a;
  a.Put(NS_LITERAL_STRING("a"), NS_LITERAL_STRING("1"),
        eCSSTokenSerialization_Nothing,
        eCSSTokenSerialization_Nothing);
  a.Put(NS_LITERAL_STRING("b"), NS_LITERAL_STRING("2"),
        eCSSTokenSerialization_Nothing,
        eCSSTokenSerialization_Nothing);

  CSSVariableValues b;
  b.Put(NS_LITERAL_STRING("b"), NS_LITERAL_STRING("2"),
        eCSSTokenSerialization_Nothing,
        eCSSTokenSerialization_Nothing);
  b.Put(NS_LITERAL_STRING("a"), NS_LITERAL_STRING("1"),
        eCSSTokenSerialization_Nothing,
        eCSSTokenSerialization_Nothing);

  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a.EqualsInOrder(b));
}
