/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * style rule processor for CSS style sheets, responsible for selector
 * matching and cascading
 */

#include "nsAutoPtr.h"
#include "nsCSSRuleProcessor.h"
#include "nsRuleProcessorData.h"
#include <algorithm>
#include "nsIAtom.h"
#include "PLDHashTable.h"
#include "nsICSSPseudoComparator.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/css/ImportRule.h"
#include "mozilla/css/StyleRule.h"
#include "mozilla/css/GroupRule.h"
#include "nsIDocument.h"
#include "nsPresContext.h"
#include "nsGkAtoms.h"
#include "nsUnicharUtils.h"
#include "nsError.h"
#include "nsRuleWalker.h"
#include "nsCSSPseudoClasses.h"
#include "nsCSSPseudoElements.h"
#include "nsIContent.h"
#include "nsCOMPtr.h"
#include "nsHashKeys.h"
#include "nsStyleUtil.h"
#include "nsQuickSort.h"
#include "nsAttrValue.h"
#include "nsAttrValueInlines.h"
#include "nsAttrName.h"
#include "nsTArray.h"
#include "nsContentUtils.h"
#include "nsIMediaList.h"
#include "nsCSSRules.h"
#include "nsStyleSet.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLSlotElement.h"
#include "mozilla/dom/ShadowRoot.h"
#include "nsNthIndexCache.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/EventStates.h"
#include "mozilla/Preferences.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/Likely.h"
#include "mozilla/OperatorNewExtensions.h"
#include "mozilla/TypedEnumBits.h"
#include "RuleProcessorCache.h"
#include "nsIDOMMutationEvent.h"
#include "nsIMozBrowserFrame.h"
#include "RuleCascadeData.h"
#include "nsCSSRuleUtils.h"
#include "CascadeLayerRuleProcessor.h"
#include "RuleProcessorGroup.h"
#include "prenv.h"
#include "nsIMemoryReporter.h"
#include <inttypes.h>

using namespace mozilla;
using namespace mozilla::dom;

// Main-thread preference caches: a rule processor is queried on hot style
// paths, so checking a live tier override must not perform a pref lookup for
// every element. Negative means tier default; zero preserves unlimited states.
static int32_t sCascadeMemoryTier = 3;
static int32_t sCascadeMaxStates = -1;
static bool sCascadeStatePrefsInitialized = false;
static bool sCascadeStateDiagnostics = false;

nsCSSRuleProcessor* nsCSSRuleProcessor::sLiveProcessors = nullptr;

// This KIND_OTHER census overlaps existing explicit ownership reports. It also
// sees live binding/CC-orphaned processors those reports may not visit. Do NOT
// use MOZ_DEFINE_MALLOC_SIZE_OF here: its DMD marking would report overlapping
// allocations a second time. Ownership reports retain their existing marking.
static size_t
CascadeDiagnosticSizeOf(const void* aPointer)
{
  return moz_malloc_size_of(aPointer);
}

class CSSCascadeMemoryReporter final : public nsIMemoryReporter
{
  ~CSSCascadeMemoryReporter() {}
public:
  NS_DECL_ISUPPORTS
  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool aAnonymize) override
  {
    // Finish the main-thread weak-list traversal before invoking callbacks,
    // which may allocate or run collection. No live processor pointers escape.
    nsCSSRuleProcessor::MemoryStats stats;
    nsCSSRuleProcessor::CollectMemoryStats(CascadeDiagnosticSizeOf, stats);
    MOZ_COLLECT_REPORT("css-cascade-memory/current-exclusive", KIND_OTHER, UNITS_BYTES,
      stats.mCurrentExclusive,
      "Exclusive storage of the most recently selected media state in each live CSS rule processor. "
      "Shared buckets are excluded. This census overlaps explicit/layout/style-sets and "
      "rule-processor-cache accounting, but also visits live binding and CC-orphaned processors. "
      "Do not add it to explicit or resident memory.");
    MOZ_COLLECT_REPORT("css-cascade-memory/other-exclusive", KIND_OTHER, UNITS_BYTES,
      stats.mOtherExclusive,
      "Exclusive storage of other retained media states, excluding shared buckets. "
      "Shared UA/user/XBL processors may use these states in other live presentation contexts. "
      "This is an alternate census overlapping explicit ownership and unclassified heap.");
    MOZ_COLLECT_REPORT("css-cascade-memory/shared-and-canonicalization", KIND_OTHER, UNITS_BYTES,
      stats.mShared,
      "Bucket backing with multiple list owners, plus weak interning tables and rule identity metadata, "
      "each counted once. Owners can be in the same state or different states. This storage is not "
      "necessarily reclaimable by dropping one state. This census overlaps explicit ownership "
      "and unclassified heap; do not add it to explicit.");
    MOZ_COLLECT_REPORT("css-cascade-memory/processor-storage", KIND_OTHER, UNITS_BYTES,
      stats.mProcessor,
      "CSS rule processor objects and their measured sheet-pointer storage, excluding state groups. "
      "Parsed stylesheets are measured separately. This census overlaps explicit ownership and "
      "unclassified heap; do not add it to explicit.");
    MOZ_COLLECT_REPORT("css-cascade-processors/one-state", KIND_OTHER, UNITS_COUNT,
      stats.mOneStateProcessors, "Live initialized CSS processors with an applied one-state retention limit.");
    MOZ_COLLECT_REPORT("css-cascade-processors/bounded", KIND_OTHER, UNITS_COUNT,
      stats.mBoundedProcessors, "Live initialized CSS processors with a positive retention limit greater than one.");
    MOZ_COLLECT_REPORT("css-cascade-processors/unlimited", KIND_OTHER, UNITS_COUNT,
      stats.mUnlimitedProcessors, "Live initialized CSS processors with unlimited state retention, including exempt processors.");
    MOZ_COLLECT_REPORT("css-cascade-processors/uninitialized", KIND_OTHER, UNITS_COUNT,
      stats.mUninitializedProcessors, "Live CSS processors without a constructed media state.");
    MOZ_COLLECT_REPORT("css-cascade-states/current", KIND_OTHER, UNITS_COUNT,
      stats.mCurrentStates, "Most recently selected state groups in live CSS rule processors.");
    MOZ_COLLECT_REPORT("css-cascade-states/other", KIND_OTHER, UNITS_COUNT,
      stats.mOtherStates, "Other retained state groups. Some shared processors serve several live contexts.");
    MOZ_COLLECT_REPORT("css-cascade-max-states-one", KIND_OTHER, UNITS_COUNT,
      stats.mMaxOne, "Maximum actual retained state count of any live processor with an applied one-state limit.");
    MOZ_COLLECT_REPORT("css-cascade-max-states-bounded", KIND_OTHER, UNITS_COUNT,
      stats.mMaxBounded, "Maximum actual retained state count of any live processor with an applied multi-state limit.");
    MOZ_COLLECT_REPORT("css-cascade-max-states-unlimited", KIND_OTHER, UNITS_COUNT,
      stats.mMaxUnlimited, "Maximum actual retained state count of any live processor with unlimited retention.");
    MOZ_COLLECT_REPORT("css-cascade-builds", KIND_OTHER, UNITS_COUNT,
      stats.mBuilds, "Complete media-state groups constructed by currently live processors. Destroyed processors' histories are excluded.");
    MOZ_COLLECT_REPORT("css-cascade-cache-hits", KIND_OTHER, UNITS_COUNT,
      stats.mHits, "Media-state cache lookup hits of currently live processors. The ordinary unchanged-context fast path is not counted.");
    MOZ_COLLECT_REPORT("css-cascade-evictions", KIND_OTHER, UNITS_COUNT,
      stats.mEvictions, "Retention-budget evictions by currently live processors. CSSOM invalidation and destruction are not budget evictions.");
    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(CSSCascadeMemoryReporter, nsIMemoryReporter)

// -------------------------------
// CSS Style rule processor implementation
//

nsCSSRuleProcessor::nsCSSRuleProcessor(const sheet_array_type& aSheets,
                                       SheetType aSheetType,
                                       Element* aScopeElement,
                                       nsCSSRuleProcessor*
                                         aPreviousCSSRuleProcessor,
                                       bool aIsShared,
                                       bool aIsBinding)
  : nsCSSRuleProcessor(sheet_array_type(aSheets), aSheetType, aScopeElement,
                       aPreviousCSSRuleProcessor, aIsShared, aIsBinding)
{
}

nsCSSRuleProcessor::nsCSSRuleProcessor(sheet_array_type&& aSheets,
                                       SheetType aSheetType,
                                       Element* aScopeElement,
                                       nsCSSRuleProcessor*
                                         aPreviousCSSRuleProcessor,
                                       bool aIsShared,
                                       bool aIsBinding)
  : mSheets(aSheets)
  , mGroup(nullptr)
  , mPreviousCacheKey(aPreviousCSSRuleProcessor
                       ? aPreviousCSSRuleProcessor->CloneMQCacheKey()
                       : UniquePtr<nsMediaQueryResultCacheKey>())
  , mLastPresContext(nullptr)
  , mScopeElement(aScopeElement)
  , mStyleSetRefCnt(0)
  , mCachedStateLimit(0)
  , mCascadeBuilds(0)
  , mCascadeHits(0)
  , mCascadeEvictions(0)
  , mSheetType(aSheetType)
  , mIsShared(aIsShared)
  , mIsBinding(aIsBinding)
  , mMustGatherDocumentRules(aIsShared)
  , mInRuleProcessorCache(false)
  , mMemoryNext(nullptr)
  , mMemoryPrevious(nullptr)
#ifdef DEBUG
  , mDocumentRulesAndCacheKeyValid(false)
#endif
{
  MOZ_ASSERT(NS_IsMainThread());
  if (!sCascadeStatePrefsInitialized) {
    Preferences::AddIntVarCache(&sCascadeMemoryTier, "browser.memoryTier", 3);
    Preferences::AddIntVarCache(&sCascadeMaxStates,
                                "layout.css.cascade-index.max-states", -1);
    sCascadeStatePrefsInitialized = true;
    const char* diagnostics = PR_GetEnv("MOZ_CSS_CASCADE_STATS");
    sCascadeStateDiagnostics = diagnostics && diagnostics[0] &&
                               diagnostics[0] != '0';
    RegisterStrongMemoryReporter(new CSSCascadeMemoryReporter());
  }
  NS_ASSERTION(!!mScopeElement == (aSheetType == SheetType::ScopedDoc),
               "aScopeElement must be specified iff aSheetType is "
               "eScopedDocSheet");
  for (sheet_array_type::size_type i = mSheets.Length(); i-- != 0; ) {
    mSheets[i]->AddRuleProcessor(this);
  }
  // Resolve the head at insertion time: initializing the reporter/service
  // above may reenter construction of another processor.
  mMemoryNext = sLiveProcessors;
  mMemoryPrevious = &sLiveProcessors;
  if (mMemoryNext) mMemoryNext->mMemoryPrevious = &mMemoryNext;
  sLiveProcessors = this;
}

nsCSSRuleProcessor::~nsCSSRuleProcessor()
{
  MOZ_ASSERT(NS_IsMainThread());
  *mMemoryPrevious = mMemoryNext;
  if (mMemoryNext) mMemoryNext->mMemoryPrevious = mMemoryPrevious;
  if (mInRuleProcessorCache) {
    RuleProcessorCache::RemoveRuleProcessor(this);
  }
  MOZ_ASSERT(!mExpirationState.IsTracked());
  MOZ_ASSERT(mStyleSetRefCnt == 0);
  ClearSheets();
  ClearGroup();
}

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsCSSRuleProcessor)
  NS_INTERFACE_MAP_ENTRY(nsIStyleRuleProcessor)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsCSSRuleProcessor)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsCSSRuleProcessor)

NS_IMPL_CYCLE_COLLECTION_CLASS(nsCSSRuleProcessor)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsCSSRuleProcessor)
  tmp->ClearSheets();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mScopeElement)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsCSSRuleProcessor)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSheets)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mScopeElement)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

void
nsCSSRuleProcessor::ClearSheets()
{
  for (sheet_array_type::size_type i = mSheets.Length(); i-- != 0; ) {
    mSheets[i]->DropRuleProcessor(this);
  }
  mSheets.Clear();
}

/* virtual */ void
nsCSSRuleProcessor::RulesMatching(ElementRuleProcessorData *aData)
{
  if (RuleProcessorGroup* group = GetGroup(aData->mPresContext)) {
    for (nsCOMPtr<nsIStyleRuleProcessor> processor : group->mItems) {
      processor->RulesMatching(aData);
    }
  }
}

/* virtual */ void
nsCSSRuleProcessor::RulesMatching(PseudoElementRuleProcessorData* aData)
{
  if (RuleProcessorGroup* group = GetGroup(aData->mPresContext)) {
    for (nsCOMPtr<nsIStyleRuleProcessor> processor : group->mItems) {
      processor->RulesMatching(aData);
    }
  }
}

/* virtual */ void
nsCSSRuleProcessor::RulesMatching(AnonBoxRuleProcessorData* aData)
{
  if (RuleProcessorGroup* group = GetGroup(aData->mPresContext)) {
    for (nsCOMPtr<nsIStyleRuleProcessor> processor : group->mItems) {
      processor->RulesMatching(aData);
    }
  }
}

/* virtual */ void
nsCSSRuleProcessor::RulesMatching(XULTreeRuleProcessorData* aData)
{
  if (RuleProcessorGroup* group = GetGroup(aData->mPresContext)) {
    for (nsCOMPtr<nsIStyleRuleProcessor> processor : group->mItems) {
      processor->RulesMatching(aData);
    }
  }
}

nsRestyleHint
nsCSSRuleProcessor::HasStateDependentStyle(ElementDependentRuleProcessorData* aData,
                                           Element* aStatefulElement,
                                           CSSPseudoElementType aPseudoType,
                                           EventStates aStateMask)
{
  MOZ_ASSERT(!aData->mTreeMatchContext.mForScopedStyle,
             "mCurrentStyleScope will need to be saved and restored after the "
             "SelectorMatchesTree call");

  nsRestyleHint hint = nsRestyleHint(0);
  if (RuleProcessorGroup* group = GetGroup(aData->mPresContext)) {
    for (nsCOMPtr<nsIStyleRuleProcessor> processor : group->mItems) {
      CascadeLayerRuleProcessor* layerProcessor =
        static_cast<CascadeLayerRuleProcessor*>(processor.get());
      layerProcessor->HasStateDependentStyle(
        aData, aStatefulElement, aPseudoType, aStateMask, hint);
    }
  }
  return hint;
}

/* virtual */ nsRestyleHint
nsCSSRuleProcessor::HasStateDependentStyle(StateRuleProcessorData* aData)
{
  return HasStateDependentStyle(aData,
                                aData->mElement,
                                CSSPseudoElementType::NotPseudo,
                                aData->mStateMask);
}

/* virtual */ nsRestyleHint
nsCSSRuleProcessor::HasStateDependentStyle(PseudoElementStateRuleProcessorData* aData)
{
  return HasStateDependentStyle(aData,
                                aData->mPseudoElement,
                                aData->mPseudoType,
                                aData->mStateMask);
}

/* virtual */ bool
nsCSSRuleProcessor::HasDocumentStateDependentStyle(StateRuleProcessorData* aData)
{
  if (RuleProcessorGroup* group = GetGroup(aData->mPresContext)) {
    for (nsCOMPtr<nsIStyleRuleProcessor> processor : group->mItems) {
      if (processor->HasDocumentStateDependentStyle(aData)) {
        return true;
      }
    }
  }

  return false;
}

nsRestyleHint
nsCSSRuleProcessor::HasAttributeDependentStyle(
    AttributeRuleProcessorData* aData,
    RestyleHintData& aRestyleHintDataResult)
{
  AttributeEnumData data(aData, aRestyleHintDataResult);
  if (RuleProcessorGroup* group = GetGroup(aData->mPresContext)) {
    for (nsCOMPtr<nsIStyleRuleProcessor> processor : group->mItems) {
      CascadeLayerRuleProcessor* layerProcessor =
        static_cast<CascadeLayerRuleProcessor*>(processor.get());
      layerProcessor->HasAttributeDependentStyle(
        aData, &data, aRestyleHintDataResult);
    }
  }
  return data.change;
}

/* virtual */ bool
nsCSSRuleProcessor::MediumFeaturesChanged(nsPresContext* aPresContext)
{
  // We don't want to do anything if there aren't any sets of rules
  // cached yet, since we should not build the rule cascade too early
  // (e.g., before we know whether the quirk style sheet should be
  // enabled).  And if there's nothing cached, it doesn't matter if
  // anything changed.  But in the cases where it does matter, we've
  // cached a previous cache key to test against, instead of our current
  // rule cascades.  See bug 448281 and bug 1089417.
  MOZ_ASSERT(!(mGroup && mPreviousCacheKey));
  RuleProcessorGroup* old = mGroup;
  if (old) {  
    RefreshGroup(aPresContext);
    return (old != mGroup);
  }

  if (mPreviousCacheKey) {
    // RefreshGroup will get rid of mPreviousCacheKey anyway to
    // maintain the invariant that we can't have both an mGroup
    // and an mPreviousCacheKey.  But we need to hold it a little
    // longer.
    UniquePtr<nsMediaQueryResultCacheKey> previousCacheKey(
      Move(mPreviousCacheKey));
    RefreshGroup(aPresContext);

    // This test is a bit pessimistic since the cache key's operator==
    // just does list comparison rather than set comparison, but it
    // should catch all the cases we care about (i.e., where the cascade
    // order hasn't changed).  Other cases will do a restyle anyway, so
    // we shouldn't need to worry about posting a second.
    return !mGroup || // all sheets gone, but we had sheets before
           mGroup->mCacheKey != *previousCacheKey;
  }

  return false;
}

/* virtual */ nsTArray<nsCOMPtr<nsIStyleRuleProcessor>>*
nsCSSRuleProcessor::GetChildRuleProcessors(nsPresContext* aPresContext)
{
  RuleProcessorGroup* group = GetGroup(aPresContext);
  return group ? &group->mItems : nullptr;
}

UniquePtr<nsMediaQueryResultCacheKey>
nsCSSRuleProcessor::CloneMQCacheKey()
{
  MOZ_ASSERT(!(mGroup && mPreviousCacheKey));

  RuleProcessorGroup* c = mGroup;
  if (!c) {
    // We might have an mPreviousCacheKey.  It already comes from a call
    // to CloneMQCacheKey, so don't bother checking
    // HasFeatureConditions().
    if (mPreviousCacheKey) {
      NS_ASSERTION(mPreviousCacheKey->HasFeatureConditions(),
                   "we shouldn't have a previous cache key unless it has "
                   "feature conditions");
      return MakeUnique<nsMediaQueryResultCacheKey>(*mPreviousCacheKey);
    }

    return UniquePtr<nsMediaQueryResultCacheKey>();
  }

  if (!c->mCacheKey.HasFeatureConditions()) {
    return UniquePtr<nsMediaQueryResultCacheKey>();
  }

  return MakeUnique<nsMediaQueryResultCacheKey>(c->mCacheKey);
}

/* virtual */ size_t
nsCSSRuleProcessor::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const
{
  size_t n = 0;
  uint32_t states = 0;
  n += mSheets.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (RuleProcessorGroup* group = mGroup; group;
       group = group->mNext) {
    n += group->SizeOfIncludingThis(aMallocSizeOf);
    ++states;
  }

  // Opt-in inline ownership evidence, emitted only during an explicit memory
  // report. No debugger attachment or enumeration through CSSOM is necessary.
  // Timing and acceptance-memory runs leave the environment switch disabled.
  if (sCascadeStateDiagnostics) {
    fprintf_stderr(stderr, "CSS_CASCADE_STATS processor=%p type=%u binding=%u "
                   "shared=%u limit=%u states=%u builds=%" PRIu64 " hits=%" PRIu64 " evictions=%" PRIu64 " "
                   "bytes=%zu\n", this, unsigned(mSheetType), unsigned(mIsBinding),
                   unsigned(mIsShared), mCachedStateLimit, states, mCascadeBuilds,
                   mCascadeHits, mCascadeEvictions, n);
  }

  return n;
}

/* static */ void
nsCSSRuleProcessor::CollectMemoryStats(MallocSizeOf aMallocSizeOf,
                                      MemoryStats& aStats)
{
  MOZ_ASSERT(NS_IsMainThread());
  for (const nsCSSRuleProcessor* processor = sLiveProcessors; processor;
       processor = processor->mMemoryNext) {
    aStats.mProcessor += aMallocSizeOf(processor) +
      processor->mSheets.ShallowSizeOfExcludingThis(aMallocSizeOf);
    uint64_t states = 0;
    for (const RuleProcessorGroup* group = processor->mGroup; group;
         group = group->mNext) {
      size_t shared = 0;
      size_t bytes = group->SizeOfIncludingThis(aMallocSizeOf, &shared);
      MOZ_RELEASE_ASSERT(shared <= bytes);
      aStats.mShared += shared;
      if (group == processor->mGroup) aStats.mCurrentExclusive += bytes - shared;
      else aStats.mOtherExclusive += bytes - shared;
      ++states;
    }
    if (states) {
      ++aStats.mCurrentStates;
      aStats.mOtherStates += states - 1;
      if (processor->mCachedStateLimit == 1) {
        ++aStats.mOneStateProcessors;
        aStats.mMaxOne = std::max(aStats.mMaxOne, states);
      } else if (processor->mCachedStateLimit) {
        ++aStats.mBoundedProcessors;
        aStats.mMaxBounded = std::max(aStats.mMaxBounded, states);
      } else {
        ++aStats.mUnlimitedProcessors;
        aStats.mMaxUnlimited = std::max(aStats.mMaxUnlimited, states);
      }
    } else {
      ++aStats.mUninitializedProcessors;
    }
    aStats.mBuilds += processor->mCascadeBuilds;
    aStats.mHits += processor->mCascadeHits;
    aStats.mEvictions += processor->mCascadeEvictions;
  }
}

/* virtual */ size_t
nsCSSRuleProcessor::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const
{
  return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
}

bool
nsCSSRuleProcessor::AppendFontFaceRules(
                              nsPresContext *aPresContext,
                              nsTArray<nsFontFaceRuleContainer>& aArray)
{
  if (RuleProcessorGroup* group = GetGroup(aPresContext)) {
    for (nsCOMPtr<nsIStyleRuleProcessor> processor : group->mItems) {
      CascadeLayerRuleProcessor* layerProcessor =
        static_cast<CascadeLayerRuleProcessor*>(processor.get());
      if (!layerProcessor->AppendFontFaceRules(aPresContext, aArray)) {
        return false;
      }
    }
  }

  return true;
}

nsCSSKeyframesRule*
nsCSSRuleProcessor::KeyframesRuleForName(nsPresContext* aPresContext,
                                         const nsString& aName)
{
  nsCSSKeyframesRule* rule = nullptr;

  if (RuleProcessorGroup* group = GetGroup(aPresContext)) {
    for (nsCOMPtr<nsIStyleRuleProcessor> processor : group->mItems) {
      CascadeLayerRuleProcessor* layerProcessor =
        static_cast<CascadeLayerRuleProcessor*>(processor.get());
      if (nsCSSKeyframesRule* newRule =
            layerProcessor->KeyframesRuleForName(aPresContext, aName)) {
        rule = newRule;
      }
    }
  }

  return rule;
}

nsCSSCounterStyleRule*
nsCSSRuleProcessor::CounterStyleRuleForName(nsPresContext* aPresContext,
                                            const nsAString& aName)
{
  nsCSSCounterStyleRule* rule = nullptr;
  if (RuleProcessorGroup* group = GetGroup(aPresContext)) {
    for (nsCOMPtr<nsIStyleRuleProcessor> processor : group->mItems) {
      CascadeLayerRuleProcessor* layerProcessor =
        static_cast<CascadeLayerRuleProcessor*>(processor.get());
      if (nsCSSCounterStyleRule* newRule =
            layerProcessor->CounterStyleRuleForName(aPresContext, aName)) {
        rule = newRule;
      }
    }
  }

  return rule;
}

bool
nsCSSRuleProcessor::AppendPageRules(
                              nsPresContext* aPresContext,
                              nsTArray<nsCSSPageRule*>& aArray)
{
  if (RuleProcessorGroup* group = GetGroup(aPresContext)) {
    for (nsCOMPtr<nsIStyleRuleProcessor> processor : group->mItems) {
      CascadeLayerRuleProcessor* layerProcessor =
        static_cast<CascadeLayerRuleProcessor*>(processor.get());
      if (!layerProcessor->AppendPageRules(aPresContext, aArray)) {
        return false;
      }
    }
  }

  return true;
}

bool
nsCSSRuleProcessor::AppendFontFeatureValuesRules(
                              nsPresContext *aPresContext,
                              nsTArray<nsCSSFontFeatureValuesRule*>& aArray)
{
  if (RuleProcessorGroup* group = GetGroup(aPresContext)) {
    for (nsCOMPtr<nsIStyleRuleProcessor> processor : group->mItems) {
      CascadeLayerRuleProcessor* layerProcessor =
        static_cast<CascadeLayerRuleProcessor*>(processor.get());
      if (!layerProcessor->AppendFontFeatureValuesRules(aPresContext, aArray)) {
        return false;
      }
    }
  }

  return true;
}

nsresult
nsCSSRuleProcessor::ClearGroup()
{
  if (!mPreviousCacheKey) {
    mPreviousCacheKey = CloneMQCacheKey();
  }

  // No need to remove the rule processor from the RuleProcessorCache here,
  // since CSSStyleSheet::ClearGroup will have called
  // RuleProcessorCache::RemoveSheet() passing itself, which will catch
  // this rule processor (and any others for different @-moz-document
  // cache key results).
  MOZ_ASSERT(!RuleProcessorCache::HasRuleProcessor(this));

#ifdef DEBUG
  // For shared rule processors, if we've already gathered document
  // rules, then they will now be out of date.  We don't actually need
  // them to be up-to-date (see the comment in RefreshGroup), so
  // record their invalidity so we can assert if we try to use them.
  if (!mMustGatherDocumentRules) {
    mDocumentRulesAndCacheKeyValid = false;
  }
#endif

  // We rely on our caller (perhaps indirectly) to do something that
  // will rebuild style data and the user font set (either
  // nsIPresShell::RestyleForCSSRuleChanges or
  // nsPresContext::RebuildAllStyleData).
  RuleProcessorGroup* data = mGroup;
  mGroup = nullptr;
  mLastPresContext = nullptr;
  while (data) {
    RuleProcessorGroup* next = data->mNext;
    delete data;
    data = next;
  }
  mIndexPool = nullptr;
  return NS_OK;
}


/**
 * Recursively traverses rules in order to:
 *  (1) add any @-moz-document rules into data->mDocumentRules.
 *  (2) record any @-moz-document rules whose conditions evaluate to true
 *      on data->mDocumentCacheKey.
 *
 * See also CascadeRuleEnumFunc below, which calls us via
 * EnumerateRulesForwards.  If modifying this function you may need to
 * update CascadeRuleEnumFunc too.
 */
static bool
GatherDocRuleEnumFunc(css::Rule* aRule, void* aData)
{
  CascadeLayer* layer = (CascadeLayer*)aData;
  int32_t type = aRule->GetType();

  MOZ_ASSERT(layer->mMustGatherDocumentRules,
             "should only call GatherDocRuleEnumFunc if "
             "mMustGatherDocumentRules is true");

  if (css::Rule::MEDIA_RULE == type || css::Rule::SUPPORTS_RULE == type ||
      css::Rule::LAYER_BLOCK_RULE == type) {
    css::GroupRule* groupRule = static_cast<css::GroupRule*>(aRule);
    if (!groupRule->EnumerateRulesForwards(GatherDocRuleEnumFunc, aData)) {
      return false;
    }
  } else if (css::Rule::DOCUMENT_RULE == type) {
    css::DocumentRule* docRule = static_cast<css::DocumentRule*>(aRule);
    if (!layer->mDocumentRules.AppendElement(docRule)) {
      return false;
    }
    if (docRule->UseForPresentation(layer->mPresContext)) {
      if (!layer->mDocumentCacheKey.AddMatchingRule(docRule)) {
        return false;
      }
    }
    if (!docRule->EnumerateRulesForwards(GatherDocRuleEnumFunc, aData)) {
      return false;
    }
  }
  return true;
}

/*
 * This enumerates style rules in a sheet (and recursively into any
 * grouping rules) in order to:
 *  (1) add any style rules, in order, into data->mRulesByWeight (for
 *      the primary CSS cascade), where they are separated by weight
 *      but kept in order per-weight, and
 *  (2) add any @font-face rules, in order, into data->mFontFaceRules.
 *  (3) add any @keyframes rules, in order, into data->mKeyframesRules.
 *  (4) add any @font-feature-value rules, in order,
 *      into data->mFontFeatureValuesRules.
 *  (5) add any @page rules, in order, into data->mPageRules.
 *  (6) add any @counter-style rules, in order, into data->mCounterStyleRules.
 *  (7) add any @-moz-document rules into data->mDocumentRules.
 *  (8) record any @-moz-document rules whose conditions evaluate to true
 *      on data->mDocumentCacheKey.
 *
 * See also GatherDocRuleEnumFunc above, which we call to traverse into
 * @-moz-document rules even if their (or an ancestor's) condition
 * fails.  This means we might look at the result of some @-moz-document
 * rules that don't actually affect whether a RuleProcessorCache lookup
 * is a hit or a miss.  The presence of @-moz-document rules inside
 * @media etc. rules should be rare, and looking at all of them in the
 * sheets lets us avoid the complication of having different document
 * cache key results for different media.
 *
 * If modifying this function you may need to update
 * GatherDocRuleEnumFunc too.
 */
static bool
CascadeRuleEnumFunc(css::Rule* aRule, void* aData)
{
  CascadeLayer* layer = (CascadeLayer*)aData;
  int32_t type = aRule->GetType();

  if (css::Rule::STYLE_RULE == type) {
    css::StyleRule* styleRule = static_cast<css::StyleRule*>(aRule);
    if (!layer->mStyleRules.AppendElement(styleRule)) {
      return false;
    }
  } else if (css::Rule::MEDIA_RULE == type ||
             css::Rule::SUPPORTS_RULE == type) {
    css::GroupRule* groupRule = static_cast<css::GroupRule*>(aRule);
    const bool use = groupRule->UseForPresentation(layer->mPresContext,
                                                   layer->mCacheKey);
    if (use || layer->mMustGatherDocumentRules) {
      if (!groupRule->EnumerateRulesForwards(
            use ? CascadeRuleEnumFunc : GatherDocRuleEnumFunc, aData)) {
        return false;
      }
    }
  } else if (css::Rule::LAYER_STATEMENT_RULE == type) {
    CSSLayerStatementRule* layerRule =
      static_cast<CSSLayerStatementRule*>(aRule);
    nsTArray<nsTArray<nsString>> pathList;
    layerRule->GetPathList(pathList);
    for (const nsTArray<nsString>& path : pathList) {
      layer->CreateNamedChildLayer(path);
    }
  } else if (css::Rule::LAYER_BLOCK_RULE == type) {
    CSSLayerBlockRule* layerRule = static_cast<CSSLayerBlockRule*>(aRule);
    nsTArray<nsString> path;
    layerRule->GetPath(path);
    nsString name;
    layerRule->GetName(name);
    CascadeLayer* targetLayer = name.IsEmpty()
                                  ? layer->CreateAnonymousChildLayer()
                                  : layer->CreateNamedChildLayer(path);
    const bool use = layerRule->UseForPresentation(layer->mPresContext,
                                                   layer->mCacheKey);
    if (use || layer->mMustGatherDocumentRules) {
      if (!layerRule->EnumerateRulesForwards(
            use ? CascadeRuleEnumFunc : GatherDocRuleEnumFunc, targetLayer)) {
        return false;
      }
    }
  } else if (css::Rule::DOCUMENT_RULE == type) {
    css::DocumentRule* docRule = static_cast<css::DocumentRule*>(aRule);
    if (layer->mMustGatherDocumentRules) {
      if (!layer->mDocumentRules.AppendElement(docRule)) {
        return false;
      }
    }
    const bool use = docRule->UseForPresentation(layer->mPresContext);
    if (use && layer->mMustGatherDocumentRules) {
      if (!layer->mDocumentCacheKey.AddMatchingRule(docRule)) {
        return false;
      }
    }
    if (use || layer->mMustGatherDocumentRules) {
      if (!docRule->EnumerateRulesForwards(
            use ? CascadeRuleEnumFunc : GatherDocRuleEnumFunc, aData)) {
        return false;
      }
    }
  } else if (css::Rule::FONT_FACE_RULE == type) {
    nsCSSFontFaceRule* fontFaceRule = static_cast<nsCSSFontFaceRule*>(aRule);
    nsFontFaceRuleContainer* ptr = layer->mData->mFontFaceRules.AppendElement();
    if (!ptr)
      return false;
    ptr->mRule = fontFaceRule;
    ptr->mSheetType = layer->mSheetType;
  } else if (css::Rule::KEYFRAMES_RULE == type) {
    nsCSSKeyframesRule* keyframesRule = static_cast<nsCSSKeyframesRule*>(aRule);
    if (!layer->mData->mKeyframesRules.AppendElement(keyframesRule)) {
      return false;
    }
  } else if (css::Rule::FONT_FEATURE_VALUES_RULE == type) {
    nsCSSFontFeatureValuesRule* fontFeatureValuesRule =
      static_cast<nsCSSFontFeatureValuesRule*>(aRule);
    if (!layer->mData->mFontFeatureValuesRules.AppendElement(
          fontFeatureValuesRule)) {
      return false;
    }
  } else if (css::Rule::PAGE_RULE == type) {
    nsCSSPageRule* pageRule = static_cast<nsCSSPageRule*>(aRule);
    if (!layer->mData->mPageRules.AppendElement(pageRule)) {
      return false;
    }
  } else if (css::Rule::COUNTER_STYLE_RULE == type) {
    nsCSSCounterStyleRule* counterStyleRule =
      static_cast<nsCSSCounterStyleRule*>(aRule);
    if (!layer->mData->mCounterStyleRules.AppendElement(counterStyleRule)) {
      return false;
    }
  } else if (css::Rule::IMPORT_RULE == type &&
             nsCSSRuleUtils::LoadImportedSheetsInOrderEnabled()) {
    css::ImportRule* importRule = static_cast<css::ImportRule*>(aRule);
    StyleSheet* sheet = importRule->GetStyleSheet();
    if (sheet) {
      nsCSSRuleProcessor::CascadeSheet(sheet->AsConcrete(), layer);
    }
  }
  return true;
}

/* static */ bool
nsCSSRuleProcessor::CascadeSheet(CSSStyleSheet* aSheet, CascadeLayer* aLayer)
{
  if (aSheet->IsApplicable() &&
      aSheet->UseForPresentation(aLayer->mPresContext,
                                 aLayer->mCacheKey) &&
      aSheet->mInner) {
    if (!nsCSSRuleUtils::LoadImportedSheetsInOrderEnabled()) {
      CSSStyleSheet* child = aSheet->mInner->mFirstChild;
      while (child) {
        CascadeSheet(child, aLayer);
        child = child->mNext;
      }
    }

    if (!aSheet->mInner->mOrderedRules.EnumerateForwards(CascadeRuleEnumFunc,
                                                         aLayer))
      return false;
  }
  return true;
}

RuleProcessorGroup*
nsCSSRuleProcessor::GetGroup(nsPresContext* aPresContext)
{
  // FIXME:  Make this infallible!

  // If anything changes about the presentation context, we will be
  // notified.  Otherwise, our cache is valid if mLastPresContext
  // matches aPresContext. Shared agent/user processors and XBL processors can
  // serve multiple contexts. The weak identity must expire with the context:
  // a new screen context may reuse the address of a destroyed print context.

  if (!mGroup || aPresContext != mLastPresContext.get() ||
      mCachedStateLimit != MediaStateLimit(aPresContext)) {
    RefreshGroup(aPresContext);
  }
  return mGroup;
}

uint32_t
nsCSSRuleProcessor::MediaStateLimit(nsPresContext* aPresContext) const
{
  // Only an individual content document's author/scoped cascade is bounded.
  // UA/user processors and XBL are shared between live documents; their
  // differing media states are a genuine simultaneous working set.
  if (mIsShared || mIsBinding || aPresContext->IsChrome() ||
      (mSheetType != SheetType::Doc && mSheetType != SheetType::ScopedDoc)) {
    return 0;
  }
  if (sCascadeMaxStates >= 0) {
    return uint32_t(sCascadeMaxStates);
  }
  return sCascadeMemoryTier <= 0 ? 1 : sCascadeMemoryTier == 1 ? 2 : 0;
}

void
nsCSSRuleProcessor::TrimMediaStates(uint32_t aLimit)
{
  mCachedStateLimit = aLimit;
  if (!aLimit || !mGroup) {
    return;
  }
  RuleProcessorGroup* last = mGroup;
  for (uint32_t count = 1; count < aLimit && last->mNext; ++count) {
    last = last->mNext;
  }
  RuleProcessorGroup* stale = last->mNext;
  last->mNext = nullptr;
  while (stale) {
    RuleProcessorGroup* next = stale->mNext;
    delete stale;
    ++mCascadeEvictions;
    stale = next;
  }
}

/**
 * This enumerates layers in a sheet and creates child rule processors
 * for each layer. The child rule processors are created in the order
 * that the layers are encountered.
 */
static void
CreateChildProcessorsEnumFunc(CascadeLayer* aLayer, void* aData)
{
  aLayer->AddRules();
  RuleProcessorGroup* data = static_cast<RuleProcessorGroup*>(aData);
  data->mItems.AppendElement(new CascadeLayerRuleProcessor(aLayer));
}

void
nsCSSRuleProcessor::RefreshGroup(nsPresContext* aPresContext)
{
  // MediumFeaturesChanged can refresh a different live context without going
  // through GetGroup. Only stamp the identity of a successfully selected group.
  mLastPresContext = nullptr;
  uint32_t stateLimit = MediaStateLimit(aPresContext);
  // Having RuleCascadeData objects be per-medium (over all variation
  // caused by media queries, handled through mCacheKey) works for now
  // since nsCSSRuleProcessor objects are per-document.  (For a given
  // set of stylesheets they can vary based on medium (@media) or
  // document (@-moz-document).)
  // When entering current-state-only mode, rebuild once without the shared
  // identity/rank infrastructure. Keep the old group alive until the new one
  // is published; MediumFeaturesChanged compares their identities, and failed
  // construction must not destroy the previous working cascade.
  bool rebuildUnshared = stateLimit == 1 && mIndexPool;
  for (RuleProcessorGroup **groupPointer = &mGroup, *group;
       (group = *groupPointer);
       groupPointer = &group->mNext) {
    if (!rebuildUnshared && group->mCacheKey.Matches(aPresContext)) {
      ++mCascadeHits;
      // Ensure that the current one is always mGroup.
      *groupPointer = group->mNext;
      group->mNext = mGroup;
      mGroup = group;

      TrimMediaStates(stateLimit);
      mCachedStateLimit = stateLimit;
      mLastPresContext = aPresContext;
      return;
    }
  }

  // We're going to make a new rule cascade; this means that we should
  // now stop using the previous cache key that we're holding on to from
  // the last time we had rule cascades.
  mPreviousCacheKey = nullptr;

  if (stateLimit == 1) {
    // Existing groups keep their own pool alive until TrimMediaStates drops
    // them. A lone current state has no cross-state buckets to canonicalize.
    mIndexPool = nullptr;
  }
  if (mSheets.Length() != 0) {
    // Canonicalizing new media states saves memory but adds construction work.
    // Prefer it on low-memory tiers; an explicit pref overrides the tier policy.
    int32_t memoryTier = Preferences::GetInt("browser.memoryTier", 3);
    bool shareIndexes = stateLimit != 1 &&
                        Preferences::GetBool("layout.css.cascade-index.share",
                                             memoryTier <= 1);
    // A single media state has nothing to share. Build its normal index first;
    // only introduce identities/canonical buckets once another state is needed.
    if (shareIndexes && !mIndexPool && mGroup) {
      mIndexPool = new CascadeIndexPool;
      for (RuleProcessorGroup* group = mGroup; group; group = group->mNext) {
        for (auto& processor : group->mItems) {
          static_cast<CascadeLayerRuleProcessor*>(processor.get())->ShareIndexes(mIndexPool);
        }
      }
    }
    nsAutoPtr<RuleProcessorGroup> ruleProcessorSet(
      new RuleProcessorGroup(aPresContext->Medium()));
    CascadeLayer* implicitLayer(new CascadeLayer(aPresContext,
#if DEBUG
                                                 nullptr,
#endif
                                                 mDocumentRules,
                                                 mDocumentCacheKey,
                                                 mSheetType,
                                                 mMustGatherDocumentRules,
                                                 ruleProcessorSet->mCacheKey,
                                                 shareIndexes ? mIndexPool.get() : nullptr));
    if (implicitLayer->mData) {
      for (uint32_t i = 0; i < mSheets.Length(); ++i) {
        if (!CascadeSheet(mSheets.ElementAt(i), implicitLayer)) {
          return; /* out of memory */
        }
      }

      // Ensure that the current one is always mGroup.
      ruleProcessorSet->mNext = mGroup;
      mGroup = ruleProcessorSet.forget();

      implicitLayer->EnumerateAllLayers(CreateChildProcessorsEnumFunc, mGroup);
      ++mCascadeBuilds;
      TrimMediaStates(stateLimit);

      // mMustGatherDocumentRules controls whether we build mDocumentRules
      // and mDocumentCacheKey so that they can be used as keys by the
      // RuleProcessorCache, as obtained by TakeDocumentRulesAndCacheKey
      // later.  We set it to false just below so that we only do this
      // the first time we build a RuleProcessorCache for a shared rule
      // processor.
      //
      // An up-to-date mDocumentCacheKey is only needed if we
      // are still in the RuleProcessorCache (as we store a copy of the
      // cache key in the RuleProcessorCache), and an up-to-date
      // mDocumentRules is only needed at the time TakeDocumentRulesAndCacheKey
      // is called, which is immediately after the rule processor is created
      // (by nsStyleSet).
      //
      // Note that when nsCSSRuleProcessor::ClearGroup is called,
      // by CSSStyleSheet::ClearGroup, we will have called
      // RuleProcessorCache::RemoveSheet, which will remove the rule
      // processor from the cache.  (This is because the list of document
      // rules now may not match the one used as they key in the
      // RuleProcessorCache.)
      //
      // Thus, as we'll no longer be in the RuleProcessorCache, and we won't
      // have TakeDocumentRulesAndCacheKey called on us, we don't need to ensure
      // mDocumentCacheKey and mDocumentRules are up-to-date after the
      // first time GetGroup is called.
      if (mMustGatherDocumentRules) {
        mDocumentRules.Sort();
        mDocumentCacheKey.Finalize();
        mMustGatherDocumentRules = false;
#ifdef DEBUG
        mDocumentRulesAndCacheKeyValid = true;
#endif
      }
      mLastPresContext = aPresContext;
    }
  }
  mCachedStateLimit = stateLimit;
  return;
}

void
nsCSSRuleProcessor::TakeDocumentRulesAndCacheKey(
    nsPresContext* aPresContext,
    nsTArray<css::DocumentRule*>& aDocumentRules,
    nsDocumentRuleResultCacheKey& aCacheKey)
{
  MOZ_ASSERT(mIsShared);

  GetGroup(aPresContext);
  MOZ_ASSERT(mDocumentRulesAndCacheKeyValid);

  aDocumentRules.Clear();
  aDocumentRules.SwapElements(mDocumentRules);
  aCacheKey.Swap(mDocumentCacheKey);

#ifdef DEBUG
  mDocumentRulesAndCacheKeyValid = false;
#endif
}

void
nsCSSRuleProcessor::AddStyleSetRef()
{
  MOZ_ASSERT(mIsShared);
  if (++mStyleSetRefCnt == 1) {
    RuleProcessorCache::StopTracking(this);
  }
}

void
nsCSSRuleProcessor::ReleaseStyleSetRef()
{
  MOZ_ASSERT(mIsShared);
  MOZ_ASSERT(mStyleSetRefCnt > 0);
  if (--mStyleSetRefCnt == 0 && mInRuleProcessorCache) {
    RuleProcessorCache::StartTracking(this);
  }
}
