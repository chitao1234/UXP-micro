/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"

#include "mozilla/css/Declaration.h"
#include "mozilla/css/StyleRule.h"

using namespace mozilla;
using namespace mozilla::css;

namespace {

size_t CountStyleAllocation(const void* aAllocation)
{
  return aAllocation ? 1 : 0;
}

already_AddRefed<Declaration> MakeMemoryTestDeclaration()
{
  RefPtr<Declaration> declaration = new Declaration();
  declaration->InitializeEmpty();
  nsCSSExpandedDataBlock expanded;
  declaration->ExpandTo(&expanded);
  expanded.AddLonghandProperty(eCSSProperty_width,
                              nsCSSValue(12.0f, eCSSUnit_Pixel));
  declaration->ValueAppended(eCSSProperty_width);

  nsCSSValue content;
  content.SetListValue()->mValue.SetStringValue(
    NS_LITERAL_STRING("memory census content"), eCSSUnit_String);
  nsCSSExpandedDataBlock important;
  important.AddLonghandProperty(eCSSProperty_content, content);
  content.Reset(); // Leave the list and string with their declaration owner.
  EXPECT_TRUE(expanded.TransferFromBlock(important, eCSSProperty_content,
    CSSEnabledState::eIgnoreEnabledState, true, true, false, declaration));
  declaration->CompressFrom(&expanded);
  return declaration.forget();
}

struct RuleMemoryParts
{
  size_t rule = 0;
  size_t selectors = 0;
  size_t declarations = 0;
  size_t metadata = 0;
  size_t blocks = 0;
  size_t nested = 0;
};

} // namespace

TEST(StyleRuleMemory, DeclarationPartsPreserveOwnedTotal)
{
  RefPtr<Declaration> declaration = MakeMemoryTestDeclaration();
  size_t metadata = 0, blocks = 0, nested = 0;
  size_t total = declaration->SizeOfIncludingThis(CountStyleAllocation,
                                                 &metadata, &blocks, &nested);
  EXPECT_EQ(size_t(1), metadata); // Order fits in the declaration's inline array.
  EXPECT_EQ(size_t(2), blocks);   // Normal and important compressed blocks.
  EXPECT_EQ(size_t(2), nested);   // The unique content list and string buffer.
  EXPECT_EQ(metadata + blocks + nested, total);
  EXPECT_EQ(total, declaration->SizeOfIncludingThis(CountStyleAllocation));
  EXPECT_EQ(total, declaration->SizeOfIncludingThis(CountStyleAllocation,
                                                    &metadata, &blocks, &nested));
  EXPECT_EQ(size_t(2), nested); // Output parameters are assigned, not accumulated.
  EXPECT_EQ(2u, declaration->Count());
}

TEST(StyleRuleMemory, ScopedCollectorRestoresContextAndIgnoresStandaloneDeclaration)
{
  RefPtr<Declaration> declaration = MakeMemoryTestDeclaration();
  auto* selector = new nsCSSSelectorList();
  selector->AddSelector(0);
  RefPtr<StyleRule> rule = new StyleRule(selector, declaration, 1, 1);
  size_t total = rule->SizeOfIncludingThis(CountStyleAllocation);

  RuleMemoryParts outer;
  {
    AutoStyleRuleSizeOfInfo outerScope(&outer.rule, &outer.selectors,
      &outer.declarations, &outer.metadata, &outer.blocks, &outer.nested);
    // Declarations outside a StyleRule must not enter its diagnostic subtotal.
    declaration->SizeOfIncludingThis(CountStyleAllocation);
    EXPECT_EQ(size_t(0), outer.metadata);
    EXPECT_EQ(size_t(0), outer.declarations);
    EXPECT_EQ(total, rule->SizeOfIncludingThis(CountStyleAllocation));
    {
      RuleMemoryParts inner;
      AutoStyleRuleSizeOfInfo innerScope(&inner.rule, &inner.selectors,
        &inner.declarations, &inner.metadata, &inner.blocks, &inner.nested);
      EXPECT_EQ(total, rule->SizeOfIncludingThis(CountStyleAllocation));
      EXPECT_EQ(total, inner.rule + inner.selectors + inner.declarations);
      EXPECT_EQ(inner.declarations, inner.metadata + inner.blocks + inner.nested);
    }
    EXPECT_EQ(total, rule->SizeOfIncludingThis(CountStyleAllocation));
  }
  EXPECT_EQ(size_t(2), outer.rule);
  EXPECT_EQ(size_t(4), outer.selectors);
  EXPECT_EQ(2 * total, outer.rule + outer.selectors + outer.declarations);
  EXPECT_EQ(outer.declarations, outer.metadata + outer.blocks + outer.nested);
  EXPECT_EQ(total, rule->SizeOfIncludingThis(CountStyleAllocation));
  EXPECT_EQ(size_t(2), outer.rule); // A closed collector no longer receives data.
}
