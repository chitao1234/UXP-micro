/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"

#include "mozilla/CSSVariableDeclarations.h"

namespace mozilla {

// Supply collisions without weakening the production hash or adding a pref.
class CSSVariableDeclarationsTest
{
public:
  static void Intern(CSSVariableDeclarations& aValues, uint64_t aHash)
  {
    aValues.InternWithHash(aHash);
  }

  static bool SharesData(const CSSVariableDeclarations& aLeft,
                         const CSSVariableDeclarations& aRight)
  {
    return aLeft.mData.get() == aRight.mData.get();
  }

  static void EnsureHashOrder(CSSVariableDeclarations& aValues)
  {
    aValues.EnsureHashOrder();
  }
};

namespace {

// Count allocations, not implementation-dependent allocator size classes.
size_t CountAllocation(const void* aAllocation)
{
  return aAllocation ? 1 : 0;
}

struct MemoryStats
{
  size_t mAllocations;
  size_t mBlocks;
  size_t mReferences;

  MemoryStats()
  {
    CSSVariableDeclarations::GetMemoryStats(CountAllocation, &mAllocations,
                                           &mBlocks, &mReferences);
  }
};

void ExpectValue(const CSSVariableDeclarations& aValues, const char16_t* aName,
                 const char16_t* aExpected)
{
  CSSVariableDeclarations::Type type;
  nsString value;
  ASSERT_TRUE(aValues.Get(nsDependentString(aName), type, value));
  EXPECT_EQ(CSSVariableDeclarations::eTokenStream, type);
  EXPECT_TRUE(value.Equals(aExpected));
}

} // namespace

TEST(CSSVariableDeclarations, CollisionKeepsOriginalMemberAndCountsBoth)
{
  MemoryStats before;
  {
    CSSVariableDeclarations first;
    first.PutTokenStream(NS_LITERAL_STRING("x"), NS_LITERAL_STRING("one"));
    CSSVariableDeclarationsTest::Intern(first, 0x53dd07);
    ASSERT_TRUE(first.IsInterned());
    MemoryStats one;
    EXPECT_EQ(before.mBlocks + 1, one.mBlocks);

    {
      CSSVariableDeclarations collision;
      collision.PutTokenStream(NS_LITERAL_STRING("x"), NS_LITERAL_STRING("two"));
      CSSVariableDeclarationsTest::Intern(collision, 0x53dd07);
      EXPECT_FALSE(collision.IsInterned());
      EXPECT_TRUE(first.IsInterned());
      EXPECT_FALSE(CSSVariableDeclarationsTest::SharesData(first, collision));
      ExpectValue(first, u"x", u"one");
      ExpectValue(collision, u"x", u"two");
      MemoryStats both;
      EXPECT_EQ(before.mBlocks + 2, both.mBlocks);
      EXPECT_EQ(before.mReferences + 2, both.mReferences);
      EXPECT_GT(both.mAllocations, one.mAllocations);

      CSSVariableDeclarations equal;
      equal.PutTokenStream(NS_LITERAL_STRING("x"), NS_LITERAL_STRING("one"));
      CSSVariableDeclarationsTest::Intern(equal, 0x53dd07);
      EXPECT_TRUE(CSSVariableDeclarationsTest::SharesData(first, equal));
      MemoryStats shared;
      EXPECT_EQ(both.mBlocks, shared.mBlocks);
      EXPECT_EQ(both.mReferences + 1, shared.mReferences);
      EXPECT_EQ(both.mAllocations, shared.mAllocations);
    }

    // Destruction of the colliding block must not remove the original entry.
    CSSVariableDeclarations equal;
    equal.PutTokenStream(NS_LITERAL_STRING("x"), NS_LITERAL_STRING("one"));
    CSSVariableDeclarationsTest::Intern(equal, 0x53dd07);
    EXPECT_TRUE(CSSVariableDeclarationsTest::SharesData(first, equal));
  }
  MemoryStats after;
  EXPECT_EQ(before.mBlocks, after.mBlocks);
  EXPECT_EQ(before.mReferences, after.mReferences);

  // The last strong reference removed the weak entry; the hash is reusable.
  CSSVariableDeclarations replacement;
  replacement.PutTokenStream(NS_LITERAL_STRING("x"), NS_LITERAL_STRING("three"));
  CSSVariableDeclarationsTest::Intern(replacement, 0x53dd07);
  EXPECT_TRUE(replacement.IsInterned());
}

TEST(CSSVariableDeclarations, SharedMutableDataIsCountedOnceAndDetaches)
{
  CSSVariableDeclarations original;
  original.PutTokenStream(NS_LITERAL_STRING("x"), NS_LITERAL_STRING("old"));
  MemoryStats before;
  CSSVariableDeclarations copy(original);
  ASSERT_TRUE(CSSVariableDeclarationsTest::SharesData(original, copy));
  MemoryStats shared;
  EXPECT_EQ(before.mBlocks, shared.mBlocks);
  EXPECT_EQ(before.mAllocations, shared.mAllocations);
  EXPECT_EQ(before.mReferences + 1, shared.mReferences);
  copy.PutTokenStream(NS_LITERAL_STRING("x"), NS_LITERAL_STRING("new"));
  EXPECT_FALSE(CSSVariableDeclarationsTest::SharesData(original, copy));
  ExpectValue(original, u"x", u"old");
  ExpectValue(copy, u"x", u"new");
  MemoryStats detached;
  EXPECT_EQ(before.mBlocks + 1, detached.mBlocks);
}

TEST(CSSVariableDeclarations, EqualValuesInDifferentOrdersStayDistinct)
{
  CSSVariableDeclarations first;
  first.PutTokenStream(NS_LITERAL_STRING("a"), NS_LITERAL_STRING("1"));
  first.PutTokenStream(NS_LITERAL_STRING("b"), NS_LITERAL_STRING("2"));
  CSSVariableDeclarationsTest::Intern(first, 0x53dd11);

  CSSVariableDeclarations reversed;
  reversed.PutTokenStream(NS_LITERAL_STRING("b"), NS_LITERAL_STRING("2"));
  reversed.PutTokenStream(NS_LITERAL_STRING("a"), NS_LITERAL_STRING("1"));
  CSSVariableDeclarationsTest::Intern(reversed, 0x53dd11);
  EXPECT_FALSE(CSSVariableDeclarationsTest::SharesData(first, reversed));

  // The first block can die before a colliding block. The latter can then
  // enter the cache itself, and must be removed again on its own destruction.
  first = CSSVariableDeclarations();
  CSSVariableDeclarationsTest::Intern(reversed, 0x53dd11);
  EXPECT_TRUE(reversed.IsInterned());
  ExpectValue(reversed, u"a", u"1");
  ExpectValue(reversed, u"b", u"2");
}

TEST(CSSVariableDeclarations, InternedCopiesDetachOnMutation)
{
  CSSVariableDeclarations original;
  original.PutTokenStream(NS_LITERAL_STRING("x"), NS_LITERAL_STRING("old"));
  CSSVariableDeclarationsTest::Intern(original, 0x53dd08);
  CSSVariableDeclarations copy(original);
  copy.Remove(NS_LITERAL_STRING("x"));
  copy.PutTokenStream(NS_LITERAL_STRING("x"), NS_LITERAL_STRING("new"));
  ExpectValue(original, u"x", u"old");
  ExpectValue(copy, u"x", u"new");
  EXPECT_TRUE(original.IsInterned());
  EXPECT_FALSE(copy.IsInterned());
  original.PutInitial(NS_LITERAL_STRING("x"));
  EXPECT_FALSE(original.IsInterned());
  CSSVariableDeclarations::Type type;
  nsString value;
  ASSERT_TRUE(original.Get(NS_LITERAL_STRING("x"), type, value));
  EXPECT_EQ(CSSVariableDeclarations::eInitial, type);
}

TEST(CSSVariableDeclarations, LocalMeasurementAlwaysIncludesWrapper)
{
  CSSVariableDeclarations values;
  EXPECT_EQ(1u, values.SizeOfIncludingThis(CountAllocation));
  values.PutTokenStream(NS_LITERAL_STRING("x"), NS_LITERAL_STRING("one"));
  EXPECT_EQ(1u, values.SizeOfIncludingThis(CountAllocation));
  CSSVariableDeclarationsTest::Intern(values, 0x53dd09);
  ASSERT_TRUE(values.IsInterned());
  EXPECT_EQ(1u, values.SizeOfIncludingThis(CountAllocation));
}

TEST(CSSVariableDeclarations, TransientOrderIndexIncludesObjectTableAndKeys)
{
  CSSVariableDeclarations values;
  values.PutTokenStream(NS_LITERAL_STRING("one"), NS_LITERAL_STRING("1"));
  values.PutTokenStream(NS_LITERAL_STRING("two"), NS_LITERAL_STRING("2"));
  MemoryStats before;
  CSSVariableDeclarationsTest::EnsureHashOrder(values);
  MemoryStats indexed;
  // Index object, hash storage, and two owned name buffers.
  EXPECT_EQ(before.mAllocations + 4, indexed.mAllocations);

  // Accumulator enumeration order is part of its state; don't intern it with
  // an ordinary authored set, even when names and values happen to match.
  CSSVariableDeclarationsTest::Intern(values, 0x53dd10);
  EXPECT_FALSE(values.IsInterned());

  CSSVariableDeclarations copy(values);
  copy.PutTokenStream(NS_LITERAL_STRING("three"), NS_LITERAL_STRING("3"));
  ExpectValue(values, u"one", u"1");
  EXPECT_FALSE(values.Has(NS_LITERAL_STRING("three")));
  ExpectValue(copy, u"three", u"3");
}

} // namespace mozilla
