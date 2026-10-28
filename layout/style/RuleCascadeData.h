/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef RuleCascadeData_h___
#define RuleCascadeData_h___

#include "mozilla/Attributes.h"
#include "mozilla/EventStates.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/RefCountType.h"
#include "mozilla/SheetType.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/css/StyleRule.h"
#include "nsContentUtils.h"
#include "nsCSSRules.h"
#include "nsExpirationTracker.h"
#include "nsIMediaList.h"
#include "nsIStyleRuleProcessor.h"
#include "nsRuleWalker.h"
#include "nsTArray.h"
#include "nsRuleProcessorData.h"

using namespace mozilla;
using namespace mozilla::dom;

struct nsFontFaceRuleContainer;
struct PerWeightData;

/**
 * A struct representing a given CSS rule and a particular selector
 * from that rule's selector list.
 */
struct RuleSelectorPair
{
  RuleSelectorPair(css::StyleRule* aRule, nsCSSSelector* aSelector)
    : mRule(aRule)
    , mSelector(aSelector)
  {
  }
  // If this class ever grows a destructor, deal with
  // PerWeightDataListItem appropriately.

  css::StyleRule* mRule;
  nsCSSSelector* mSelector; // which of |mRule|'s selectors
};

/**
 * A struct representing a particular rule in an ordered list of rules
 * (the ordering depending on the weight of mSelector and the order of
 * our rules to start with).
 */
struct RuleValue : RuleSelectorPair
{
  enum
  {
    eMaxAncestorHashes = 4
  };

  RuleValue(const RuleSelectorPair& aRuleSelectorPair,
            int32_t aIndex,
            bool aQuirksMode)
    : RuleSelectorPair(aRuleSelectorPair)
    , mIndex(aIndex)
  {
    CollectAncestorHashes(aQuirksMode);
  }

  // A local weight/order rank before sharing; a stable identity after sharing.
  // RuleHash::Rank resolves the latter through this media state's rank view.
  int32_t mIndex;
  uint32_t mAncestorSelectorHashes[eMaxAncestorHashes];

  bool operator==(const RuleValue& aOther) const {
    if (mRule != aOther.mRule || mSelector != aOther.mSelector || mIndex != aOther.mIndex)
      return false;
    for (size_t i = 0; i < eMaxAncestorHashes; ++i)
      if (mAncestorSelectorHashes[i] != aOther.mAncestorSelectorHashes[i]) return false;
    return true;
  }

private:
  void CollectAncestorHashes(bool aQuirksMode)
  {
    // Collect up our mAncestorSelectorHashes.  It's not clear whether it's
    // better to stop once we've found eMaxAncestorHashes of them or to keep
    // going and preferentially collect information from selectors higher up the
    // chain...  Let's do the former for now.
    size_t hashIndex = 0;
    for (nsCSSSelector* sel = mSelector->mNext; sel; sel = sel->mNext) {
      if (!NS_IS_ANCESTOR_OPERATOR(sel->mOperator)) {
        // |sel| is going to select something that's not actually one of our
        // ancestors, so don't add it to mAncestorSelectorHashes.  But keep
        // going, because it'll select a sibling of one of our ancestors, so its
        // ancestors would be our ancestors too.
        continue;
      }

      // Now sel is supposed to select one of our ancestors.  Grab
      // whatever info we can from it into mAncestorSelectorHashes.
      // But in qurks mode, don't grab IDs and classes because those
      // need to be matched case-insensitively.
      if (!aQuirksMode) {
        nsAtomList* ids = sel->mIDList;
        while (ids) {
          mAncestorSelectorHashes[hashIndex++] = ids->mAtom->hash();
          if (hashIndex == eMaxAncestorHashes) {
            return;
          }
          ids = ids->mNext;
        }

        nsAtomList* classes = sel->mClassList;
        while (classes) {
          mAncestorSelectorHashes[hashIndex++] = classes->mAtom->hash();
          if (hashIndex == eMaxAncestorHashes) {
            return;
          }
          classes = classes->mNext;
        }
      }

      // Only put in the tag name if it's all-lowercase.  Otherwise we run into
      // trouble because we may test the wrong one of mLowercaseTag and
      // mCasedTag against the filter.
      if (sel->mLowercaseTag && sel->mCasedTag == sel->mLowercaseTag) {
        mAncestorSelectorHashes[hashIndex++] = sel->mLowercaseTag->hash();
        if (hashIndex == eMaxAncestorHashes) {
          return;
        }
      }
    }

    while (hashIndex != eMaxAncestorHashes) {
      mAncestorSelectorHashes[hashIndex++] = 0;
    }
  }
};

/**
 * A struct that stores an nsCSSSelector pointer along side a pointer to
 * the rightmost nsCSSSelector in the selector.  For example, for
 *
 *   .main p > span
 *
 * if mSelector points to the |p| nsCSSSelector, mRightmostSelector would
 * point to the |span| nsCSSSelector.
 *
 * Both mSelector and mRightmostSelector are always top-level selectors,
 * i.e. they aren't selectors within a :not() or :-moz-any().
 */
struct SelectorPair
{
  SelectorPair(nsCSSSelector* aSelector, nsCSSSelector* aRightmostSelector)
    : mSelector(aSelector)
    , mRightmostSelector(aRightmostSelector)
  {
    MOZ_ASSERT(aSelector);
    MOZ_ASSERT(mRightmostSelector);
  }
  SelectorPair(const SelectorPair& aOther) = default;
  nsCSSSelector* const mSelector;
  nsCSSSelector* const mRightmostSelector;
  bool operator==(const SelectorPair& aOther) const {
    return mSelector == aOther.mSelector && mRightmostSelector == aOther.mRightmostSelector;
  }
};

struct StateSelector
{
  StateSelector(mozilla::EventStates aStates, nsCSSSelector* aSelector)
    : mStates(aStates)
    , mSelector(aSelector)
  {
  }

  mozilla::EventStates mStates;
  nsCSSSelector* mSelector;
  bool operator==(const StateSelector& aOther) const {
    return mStates == aOther.mStates && mSelector == aOther.mSelector;
  }
};

#include "CascadeIndex.h"

inline uint32_t CascadeIndexHash(const RuleValue& aValue) {
  uint32_t hash = HashGeneric(aValue.mRule, aValue.mSelector, aValue.mIndex);
  for (uint32_t value : aValue.mAncestorSelectorHashes) hash = AddToHash(hash, value);
  return hash;
}
inline uint32_t CascadeIndexHash(const SelectorPair& aValue) {
  return HashGeneric(aValue.mSelector, aValue.mRightmostSelector);
}
inline uint32_t CascadeIndexHash(const StateSelector& aValue) {
  return HashGeneric(aValue.mSelector, aValue.mStates.GetInternalValue());
}

class RuleHash
{
public:
  explicit RuleHash(bool aQuirksMode, CascadeIndexPool* aPool = nullptr);
  ~RuleHash();
  void AppendRule(const RuleSelectorPair& aRuleInfo);
  void EnumerateAllRules(Element* aElement,
                         ElementDependentRuleProcessorData* aData,
                         NodeMatchContext& aNodeMatchContext);
  void Freeze();
  void ShareIndexes(CascadeIndexPool* aPool);

  size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf,
                           size_t* aSharedBytes = nullptr) const;
  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf,
                           size_t* aSharedBytes = nullptr) const;

  static void AppendRuleToTagTable(PLDHashTable* aTable,
                                   nsIAtom* aKey,
                                   const RuleValue& aRuleInfo);

  static const PLDHashTableOps TagTable_Ops;
  static const PLDHashTableOps ClassTable_CSOps;
  static const PLDHashTableOps ClassTable_CIOps;
  static const PLDHashTableOps IdTable_CSOps;
  static const PLDHashTableOps IdTable_CIOps;
  static const PLDHashTableOps NameSpaceTable_Ops;

protected:
  typedef CascadeIndexList<RuleValue, 1> RuleValueList;
  RuleValue MakeValue(const RuleSelectorPair& aRule, int32_t aRank);
  int32_t Rank(const RuleValue& aValue) const {
    if (!mPool) return aValue.mIndex;
    if (mSparseRanks.IsEmpty()) return mRanks[uint32_t(aValue.mIndex) - mRankBase];
    size_t lo = 0, hi = mSparseRanks.Length();
    while (lo < hi) {
      size_t mid = lo + (hi - lo) / 2;
      if (mSparseRanks[mid].mId < uint32_t(aValue.mIndex)) lo = mid + 1;
      else hi = mid;
    }
    MOZ_ASSERT(lo < mSparseRanks.Length() && mSparseRanks[lo].mId == uint32_t(aValue.mIndex));
    return mSparseRanks[lo].mRank;
  }
  void AppendRuleToTable(PLDHashTable* aTable,
                         const void* aKey,
                         const RuleSelectorPair& aRuleInfo);
  void AppendUniversalRule(const RuleSelectorPair& aRuleInfo);

  int32_t mRuleCount;

  PLDHashTable mIdTable;
  PLDHashTable mClassTable;
  PLDHashTable mTagTable;
  PLDHashTable mNameSpaceTable;
  RuleValueList mUniversalRules;

  struct EnumData
  {
    const RuleValue* mCurValue;
    const RuleValue* mEnd;
  };
  EnumData* mEnumList;
  int32_t mEnumListSize;

  bool mQuirksMode;
  RefPtr<CascadeIndexPool> mPool;
  nsTArray<int32_t> mRanks;
  uint32_t mRankBase = 0;
  struct SparseRank { uint32_t mId; int32_t mRank; };
  nsTArray<SparseRank> mSparseRanks;
  bool mFrozen = false;

  inline EnumData ToEnumData(const RuleValueList& arr)
  {
    EnumData data = { arr.Elements(), arr.Elements() + arr.Length() };
    return data;
  }

#ifdef RULE_HASH_STATS
  uint32_t mUniversalSelectors;
  uint32_t mNameSpaceSelectors;
  uint32_t mTagSelectors;
  uint32_t mClassSelectors;
  uint32_t mIdSelectors;

  uint32_t mElementsMatched;

  uint32_t mElementUniversalCalls;
  uint32_t mElementNameSpaceCalls;
  uint32_t mElementTagCalls;
  uint32_t mElementClassCalls;
  uint32_t mElementIdCalls;
#endif // RULE_HASH_STATS
};

struct AttributeEnumData
{
  AttributeEnumData(AttributeRuleProcessorData* aData,
                    RestyleHintData& aRestyleHintData)
    : data(aData)
    , change(nsRestyleHint(0))
    , hintData(aRestyleHintData)
  {
  }

  AttributeRuleProcessorData* data;
  nsRestyleHint change;
  RestyleHintData& hintData;
};

struct RuleCascadeData
{
  RuleCascadeData(bool aQuirksMode, CascadeIndexPool* aPool = nullptr);
  ~RuleCascadeData();

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf,
                           size_t* aSharedBytes = nullptr) const;

  RuleHash mRuleHash;
  RuleHash* mPseudoElementRuleHashes[static_cast<CSSPseudoElementTypeBase>(
    CSSPseudoElementType::Count)];
  CascadeIndexList<StateSelector> mStateSelectors;
  EventStates mSelectorDocumentStates;
  PLDHashTable mClassSelectors;
  PLDHashTable mIdSelectors;
  CascadeIndexList<nsCSSSelector*> mPossiblyNegatedClassSelectors;
  CascadeIndexList<nsCSSSelector*> mPossiblyNegatedIDSelectors;
  PLDHashTable mAttributeSelectors;
  PLDHashTable mAnonBoxRules;
  PLDHashTable mXULTreeRules;

  nsTArray<nsFontFaceRuleContainer> mFontFaceRules;
  nsTArray<nsCSSKeyframesRule*> mKeyframesRules;
  nsTArray<nsCSSFontFeatureValuesRule*> mFontFeatureValuesRules;
  nsTArray<nsCSSPageRule*> mPageRules;
  nsTArray<nsCSSCounterStyleRule*> mCounterStyleRules;

  nsDataHashtable<nsStringHashKey, nsCSSKeyframesRule*> mKeyframesRuleTable;
  nsDataHashtable<nsStringHashKey, nsCSSCounterStyleRule*>
    mCounterStyleRuleTable;

  // Looks up or creates the appropriate list in |mAttributeSelectors|.
  // Returns null only on allocation failure.
  CascadeIndexList<SelectorPair, 2>* AttributeListFor(nsIAtom* aAttribute);
  void Freeze();
  void ShareIndexes(CascadeIndexPool* aPool);
  RefPtr<CascadeIndexPool> mIndexPool;

  const bool mQuirksMode;

  void RulesMatching(ElementRuleProcessorData* aData);

  void RulesMatching(PseudoElementRuleProcessorData* aData);

  void RulesMatching(AnonBoxRuleProcessorData* aData);

  void RulesMatching(XULTreeRuleProcessorData* aData);

  void HasStateDependentStyle(ElementDependentRuleProcessorData* aData,
                              Element* aStatefulElement,
                              CSSPseudoElementType aPseudoType,
                              EventStates aStateMask,
                              nsRestyleHint& aHint);

  void HasAttributeDependentStyle(
    AttributeRuleProcessorData* aData,
    AttributeEnumData* aEnumData,
    mozilla::RestyleHintData& aRestyleHintDataResult);

  bool AddSelector(
    // The part between combinators at the top level of the selector
    nsCSSSelector* aSelectorInTopLevel,
    // The part we should look through (might be in :not or :-moz-any())
    nsCSSSelector* aSelectorPart,
    // The right-most selector at the top level
    nsCSSSelector* aRightmostSelector);

  bool AddRule(RuleSelectorPair* aRuleInfo);

private:
  static const PLDHashTableOps AtomSelector_CSOps;
  static const PLDHashTableOps AtomSelector_CIOps;
};

struct CascadeLayer
{
  CascadeLayer(nsPresContext* aPresContext,
#ifdef DEBUG
               CascadeLayer* aParent,
#endif
               nsTArray<css::DocumentRule*>& aDocumentRules,
               nsDocumentRuleResultCacheKey& aDocumentKey,
               SheetType aSheetType,
               bool aMustGatherDocumentRules,
               nsMediaQueryResultCacheKey& aCacheKey,
               CascadeIndexPool* aIndexPool = nullptr);
  ~CascadeLayer();

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf,
                           size_t* aSharedBytes = nullptr) const;

  nsPresContext* mPresContext;
  nsString mName;
  bool mIsAnonymous;
#ifdef DEBUG
  bool mIsStrong;
#endif
  bool mRulesAdded;

  RuleCascadeData* mData;

  nsTArray<css::StyleRule*> mStyleRules;
  nsTArray<css::DocumentRule*>& mDocumentRules;
  nsDocumentRuleResultCacheKey& mDocumentCacheKey;
  nsMediaQueryResultCacheKey& mCacheKey;
  SheetType mSheetType;
  bool mMustGatherDocumentRules;

#ifdef DEBUG
  CascadeLayer* mParent;
#endif
  nsTArray<CascadeLayer*> mPreLayers;
#ifdef DEBUG
  nsTArray<CascadeLayer*> mPostLayers;
#endif
  nsDataHashtable<nsStringHashKey, CascadeLayer*> mLayers;

  CascadeLayer* CreateNamedChildLayer(const nsTArray<nsString>& aPath);
  CascadeLayer* CreateAnonymousChildLayer();

  typedef void (*nsLayerEnumFunc)(CascadeLayer* aLayer, void* aData);
  void EnumerateAllLayers(nsLayerEnumFunc aFunc, void* aData);
  void AddRules();

private:
  struct WeightedRuleData
  {
    WeightedRuleData();
    ~WeightedRuleData();

    UniquePtr<PerWeightData[]> Consume(nsTArray<css::StyleRule*>& aStyleRules);

    PLArenaPool mArena;
    // Hooray, a manual PLDHashTable since nsClassHashtable doesn't
    // provide a getter that gives me a *reference* to the value.
    PLDHashTable mRulesByWeight; // of PerWeightDataListItem linked lists

  private:
    static const PLDHashTableOps sRulesByWeightOps;
  };

};

#endif /* RuleCascadeData_h___ */
