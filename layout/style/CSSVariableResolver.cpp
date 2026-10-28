/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* object that resolves CSS variables using specified and inherited variable
 * values
 */

#include "CSSVariableResolver.h"

#include "CSSVariableDeclarations.h"
#include "CSSVariableValues.h"
#include "mozilla/PodOperations.h"
#include "mozilla/Preferences.h"
#include "mozilla/UniquePtr.h"
#include "nsIMemoryReporter.h"
#include "nsThreadUtils.h"
#include <algorithm>

namespace mozilla {

// Main-thread work counters, independent of allocation ownership. Keep them in
// the built-in reports so a sparse-graph speedup can be attributed without
// temporary console instrumentation or treating operations as heap bytes.
static uint64_t sResolverCalls;
static uint64_t sResolverVariables;
static uint64_t sResolverBitmapSlotsCleared;
static uint64_t sResolverReferenceSlotsVisited;
static uint64_t sResolverEdges;
static bool sSparseResolution = true;

class CSSVariableResolverReporter final : public nsIMemoryReporter
{
  ~CSSVariableResolverReporter() {}
public:
  NS_DECL_ISUPPORTS
  NS_IMETHOD CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool aAnonymize) override
  {
    MOZ_ASSERT(NS_IsMainThread());
    // Snapshot before callbacks, which may themselves allocate or resolve CSS.
    uint64_t calls = sResolverCalls, variables = sResolverVariables;
    uint64_t cleared = sResolverBitmapSlotsCleared;
    uint64_t visited = sResolverReferenceSlotsVisited, edges = sResolverEdges;
#define RESOLVER_COUNT(path, value, description) \
    MOZ_COLLECT_REPORT("css-variable-resolver/" path, KIND_OTHER, \
                       UNITS_COUNT_CUMULATIVE, value, description)
    RESOLVER_COUNT("calls", calls, "Completed custom-property resolution calls.");
    RESOLVER_COUNT("variables", variables,
      "Custom properties visited by resolution, including inherited computed values.");
    RESOLVER_COUNT("bitmap-slots-cleared", cleared,
      "Dependency bitmap slots cleared while building custom-property graphs. "
      "This counts work, not retained memory.");
    RESOLVER_COUNT("reference-slots-visited", visited,
      "Bitmap or sparse-list slots visited when copying dependency edges. "
      "Sorting comparisons are not included.");
    RESOLVER_COUNT("edges", edges,
      "Unique dependency edges built for custom-property resolution.");
#undef RESOLVER_COUNT
    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(CSSVariableResolverReporter, nsIMemoryReporter)

/**
 * Data used by the EnumerateVariableReferences callback.  Reset must be called
 * on it before it is used.
 */
class EnumerateVariableReferencesData
{
public:
  EnumerateVariableReferencesData(CSSVariableResolver& aResolver, bool aSparse)
    : mResolver(aResolver)
    , mReferences(MakeUnique<bool[]>(aResolver.mVariables.Length()))
    , mSparse(aSparse)
    , mDense(false)
    , mReferencesNonExistentVariable(false)
    , mSlotsCleared(aResolver.mVariables.Length())
    , mSlotsVisited(0)
  {
    // MakeUnique<T[]> value-initializes the bitmap once. Sparse resets below
    // touch only entries set by the preceding specified value.
  }

  /**
   * Resets the data so that it can be passed to another call of
   * EnumerateVariableReferences for a different variable.
   */
  void Reset()
  {
    if (mSparse && !mDense) {
      for (size_t id : mReferencedIDs) {
        mReferences[id] = false;
      }
      mSlotsCleared += mReferencedIDs.Length();
      mReferencedIDs.Clear();
    } else {
      PodZero(mReferences.get(), mResolver.mVariables.Length());
      mSlotsCleared += mResolver.mVariables.Length();
    }
    mDense = false;
    mReferencesNonExistentVariable = false;
  }

  void RecordVariableReference(const nsAString& aVariableName)
  {
    size_t id;
    if (mResolver.mVariableIDs.Get(aVariableName, &id)) {
      if (!mSparse) {
        mReferences[id] = true;
      } else if (!mReferences[id]) {
        mReferences[id] = true;
        if (!mDense) {
          mReferencedIDs.AppendElement(id);
          size_t limit = std::max(size_t(8), mResolver.mVariables.Length() / 8);
          if (mReferencedIDs.Length() > limit) {
            // A dense graph uses the bitmap for copying/resetting. Stop
            // growing the scratch list; callbacks still deduplicate there.
            mDense = true;
            mReferencedIDs.Clear();
          }
        }
      }
    } else {
      mReferencesNonExistentVariable = true;
    }
  }

  bool HasReferenceToVariable(size_t aID) const
  {
    return mReferences[aID];
  }

  bool ReferencesNonExistentVariable() const
  {
   return mReferencesNonExistentVariable;
  }

  void CopyReferencesTo(nsTArray<size_t>& aReferences)
  {
    size_t count = mResolver.mVariables.Length();
    // Preserve the historical ascending-ID traversal and hence serialization
    // order. Dense graphs use the bounded bitmap scan instead of O(n log n)
    // sorting. Repeated references never grow the list beyond count entries.
    if (mSparse && !mDense) {
      if (mReferencedIDs.Length() > 1) {
        std::sort(mReferencedIDs.begin(), mReferencedIDs.end());
      }
      aReferences.AppendElements(mReferencedIDs);
      mSlotsVisited += mReferencedIDs.Length();
      return;
    }
    for (size_t id = 0; id < count; id++) {
      if (mReferences[id]) {
        aReferences.AppendElement(id);
      }
    }
    mSlotsVisited += count;
  }

  uint64_t SlotsCleared() const { return mSlotsCleared; }
  uint64_t SlotsVisited() const { return mSlotsVisited; }

private:
  CSSVariableResolver& mResolver;

  // Array of booleans, where each index is a variable ID.  If an element is
  // true, it indicates that the variable we have called
  // EnumerateVariableReferences for has a reference to the variable with
  // that ID.
  const UniquePtr<bool[]> mReferences;
  AutoTArray<size_t, 8> mReferencedIDs;
  const bool mSparse;
  bool mDense;

  // Whether the variable we have called EnumerateVariableReferences for
  // references a variable that does not exist in the resolver.
  bool mReferencesNonExistentVariable;
  uint64_t mSlotsCleared;
  uint64_t mSlotsVisited;
};

static void
RecordVariableReference(const nsAString& aVariableName,
                        void* aData)
{
  static_cast<EnumerateVariableReferencesData*>(aData)->
    RecordVariableReference(aVariableName);
}

void
CSSVariableResolver::RemoveCycles(size_t v)
{
  mVariables[v].mIndex = mNextIndex;
  mVariables[v].mLowLink = mNextIndex;
  mVariables[v].mInStack = true;
  mStack.AppendElement(v);
  mNextIndex++;

  for (size_t i = 0, n = mReferences[v].Length(); i < n; i++) {
    size_t w = mReferences[v][i];
    if (!mVariables[w].mIndex) {
      RemoveCycles(w);
      mVariables[v].mLowLink = std::min(mVariables[v].mLowLink,
                                        mVariables[w].mLowLink);
    } else if (mVariables[w].mInStack) {
      mVariables[v].mLowLink = std::min(mVariables[v].mLowLink,
                                        mVariables[w].mIndex);
    }
  }

  if (mVariables[v].mLowLink == mVariables[v].mIndex) {
    if (mStack.LastElement() == v) {
      // A strongly connected component consisting of a single variable is not
      // necessarily invalid.  We handle variables that reference themselves
      // earlier, in CSSVariableResolver::Resolve.
      mVariables[mStack.LastElement()].mInStack = false;
      mStack.TruncateLength(mStack.Length() - 1);
    } else {
      size_t w;
      do {
        w = mStack.LastElement();
        mVariables[w].mValue.Truncate(0);
        mVariables[w].mInStack = false;
        mStack.TruncateLength(mStack.Length() - 1);
      } while (w != v);
    }
  }
}

void
CSSVariableResolver::ResolveVariable(size_t aID)
{
  if (mVariables[aID].mValue.IsEmpty() || mVariables[aID].mWasInherited) {
    // The variable is invalid or was inherited.   We can just copy the value
    // and its first/last token information across.
    mOutput->Put(mVariables[aID].mVariableName,
                 mVariables[aID].mValue,
                 mVariables[aID].mFirstToken,
                 mVariables[aID].mLastToken);
  } else {
    // Otherwise we need to resolve the variable references, after resolving
    // all of our dependencies first.  We do this even for variables that we
    // know do not reference other variables so that we can find their
    // first/last token.
    //
    // XXX We might want to do this first/last token finding during
    // EnumerateVariableReferences, so that we can avoid calling
    // ResolveVariableValue and parsing the value again.
    for (size_t i = 0, n = mReferences[aID].Length(); i < n; i++) {
      size_t j = mReferences[aID][i];
      if (aID != j && !mVariables[j].mResolved) {
        ResolveVariable(j);
      }
    }
    nsString resolvedValue;
    nsCSSTokenSerializationType firstToken = eCSSTokenSerialization_Nothing;
    nsCSSTokenSerializationType lastToken = eCSSTokenSerialization_Nothing;
    if (!mParser.ResolveVariableValue(mVariables[aID].mValue, mOutput,
                                      resolvedValue, firstToken, lastToken)) {
      resolvedValue.Truncate(0);
      firstToken = eCSSTokenSerialization_Nothing;
      lastToken = eCSSTokenSerialization_Nothing;
    }
    mOutput->Put(mVariables[aID].mVariableName, resolvedValue,
                 firstToken, lastToken);
  }
  mVariables[aID].mResolved = true;
}

void
CSSVariableResolver::Resolve(const CSSVariableValues* aInherited,
                             const CSSVariableDeclarations* aSpecified)
{
  MOZ_ASSERT(!mResolved);

  // The only time we would be worried about having a null aInherited is
  // for the root, but in that case nsRuleNode::ComputeVariablesData will
  // happen to pass in whatever we're using as mOutput for aInherited,
  // which will initially be empty.
  MOZ_ASSERT(aInherited);
  MOZ_ASSERT(aSpecified);
  MOZ_ASSERT(NS_IsMainThread());
  static bool reporterInitialized = false;
  if (!reporterInitialized) {
    reporterInitialized = true;
    Preferences::AddBoolVarCache(&sSparseResolution,
                                 "layout.css.variables.sparse-resolution", true);
    RegisterStrongMemoryReporter(new CSSVariableResolverReporter());
  }
  bool sparse = sSparseResolution;

  aInherited->AddVariablesToResolver(this);
  aSpecified->AddVariablesToResolver(this);

  // First we look at each variable's value and record which other variables
  // it references.
  size_t n = mVariables.Length();
  // For small maps, bitmap operations are cheaper than managing a sparse
  // list. The control and small-map paths retain the historical behavior.
  sparse &= n >= 32;
  mReferences.SetLength(n);
  EnumerateVariableReferencesData data(*this, sparse);
  for (size_t id = 0; id < n; id++) {
    if (!sparse) {
      data.Reset();
    }
    if (!mVariables[id].mWasInherited &&
        !mVariables[id].mValue.IsEmpty()) {
      if (sparse) {
        data.Reset();
      }
      if (mParser.EnumerateVariableReferences(mVariables[id].mValue,
                                              RecordVariableReference,
                                              &data)) {
        // Convert the boolean array of dependencies in |data| to a list
        // of dependencies.
        data.CopyReferencesTo(mReferences[id]);
        // If a variable references itself, it is invalid.  (RemoveCycles
        // does not check for cycles consisting of a single variable, so we
        // check here.)
        if (data.HasReferenceToVariable(id)) {
          mVariables[id].mValue.Truncate();
        }
        // Also record whether it referenced any variables that don't exist
        // in the resolver, so that we can ensure we still resolve its value
        // in ResolveVariable, even though its mReferences list is empty.
        mVariables[id].mReferencesNonExistentVariable =
          data.ReferencesNonExistentVariable();
      } else {
        MOZ_ASSERT(false, "EnumerateVariableReferences should not have failed "
                          "if we previously parsed the specified value");
        mVariables[id].mValue.Truncate(0);
      }
    }
  }

  // Next we remove any cycles in variable references using Tarjan's strongly
  // connected components finding algorithm, setting variables in cycles to
  // have an invalid value.
  mNextIndex = 1;
  uint64_t edges = 0;
  for (size_t id = 0; id < n; id++) {
    edges += mReferences[id].Length();
    if (!mVariables[id].mIndex) {
      RemoveCycles(id);
      MOZ_ASSERT(mStack.IsEmpty());
    }
  }

  // Finally we construct the computed value for the variable by substituting
  // any variable references.
  for (size_t id = 0; id < n; id++) {
    if (!mVariables[id].mResolved) {
      ResolveVariable(id);
    }
  }

  sResolverCalls++;
  sResolverVariables += n;
  sResolverBitmapSlotsCleared += data.SlotsCleared();
  sResolverReferenceSlotsVisited += data.SlotsVisited();
  sResolverEdges += edges;

#ifdef DEBUG
  mResolved = true;
#endif
}

void
CSSVariableResolver::Put(const nsAString& aVariableName,
                         nsString aValue,
                         nsCSSTokenSerializationType aFirstToken,
                         nsCSSTokenSerializationType aLastToken,
                         bool aWasInherited)
{
  MOZ_ASSERT(!mResolved);

  size_t id;
  if (mVariableIDs.Get(aVariableName, &id)) {
    MOZ_ASSERT(mVariables[id].mWasInherited && !aWasInherited,
               "should only overwrite inherited variables with specified "
               "variables");
    mVariables[id].mValue = aValue;
    mVariables[id].mFirstToken = aFirstToken;
    mVariables[id].mLastToken = aLastToken;
    mVariables[id].mWasInherited = aWasInherited;
  } else {
    id = mVariables.Length();
    mVariableIDs.Put(aVariableName, id);
    mVariables.AppendElement(Variable(aVariableName, aValue,
                                      aFirstToken, aLastToken, aWasInherited));
  }
}

} // namespace mozilla
