/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* computed CSS Variable values */

#include "CSSVariableValues.h"

#include "CSSVariableResolver.h"

#include "mozilla/Mutex.h"
#include "mozilla/Preferences.h"
#include "mozilla/RefCounted.h"
#include "nsAutoPtr.h"
#include "nsHashKeys.h"
#include "nsTHashtable.h"
#include "nsThreadUtils.h"
#include <cstring>

namespace mozilla {

// A live-value registry lets the memory reporter account for the heap data
// owned by every CSSVariableValues object without walking style contexts.
// The registry and its lock are intentionally process-lifetime objects: style
// values can be destroyed during shutdown after static destructors have run.
static Mutex* sLiveValuesMutex = new Mutex("CSSVariableValues");
static nsTArray<CSSVariableValues*>* sLiveValues =
  new nsTArray<CSSVariableValues*>();

// Weak cache of immutable computed variable sets.  Entries are removed by the
// Data destructor, so the cache never owns a set that has no style context.
static Mutex* sDataCacheMutex = new Mutex("CSSVariableValuesData");
static nsDataHashtable<nsUint64HashKey, void*>* sDataCache =
  new nsDataHashtable<nsUint64HashKey, void*>();

static void
RegisterVariableValues(CSSVariableValues* aValues)
{
  MutexAutoLock lock(*sLiveValuesMutex);
  sLiveValues->AppendElement(aValues);
}

static void
UnregisterVariableValues(CSSVariableValues* aValues)
{
  MutexAutoLock lock(*sLiveValuesMutex);
  size_t index = sLiveValues->IndexOf(aValues);
  if (index != nsTArray<CSSVariableValues*>::NoIndex) {
    sLiveValues->RemoveElementAt(index);
  }
}

struct CSSVariableValues::Data : public RefCounted<Data>
{
  struct Variable
  {
    Variable(uint32_t aNameOffset,
             uint32_t aNameLength,
             uint32_t aValueOffset,
             uint32_t aValueLength,
             uint32_t aTokens,
             uint32_t aBaseIndex = UINT32_MAX)
      : mNameOffset(aNameOffset)
      , mNameLength(aNameLength)
      , mValueOffset(aValueOffset)
      , mValueLength(aValueLength)
      , mTokens(aTokens)
      , mBaseIndex(aBaseIndex)
    {}

    static uint32_t PackTokens(nsCSSTokenSerializationType aFirstToken,
                               nsCSSTokenSerializationType aLastToken)
    {
      MOZ_ASSERT(static_cast<uint32_t>(aFirstToken) <= 0xffff);
      MOZ_ASSERT(static_cast<uint32_t>(aLastToken) <= 0xffff);
      return static_cast<uint32_t>(aFirstToken) |
             (static_cast<uint32_t>(aLastToken) << 16);
    }

    nsCSSTokenSerializationType FirstToken() const
    {
      return static_cast<nsCSSTokenSerializationType>(mTokens & 0xffff);
    }

    nsCSSTokenSerializationType LastToken() const
    {
      return static_cast<nsCSSTokenSerializationType>(mTokens >> 16);
    }

    uint32_t mNameOffset;
    uint32_t mNameLength;
    uint32_t mValueOffset;
    uint32_t mValueLength;
    uint32_t mTokens;
    uint32_t mBaseIndex;
  };

  MOZ_DECLARE_REFCOUNTED_TYPENAME(Data)

  Data()
    : mUseCompact(Preferences::GetBool("layout.css.variables.compact", true))
    , mIndexMask(0)
    , mHash(0)
    , mFrozen(false)
    , mInCache(false)
    , mFactorFailure(NoFailure)
  {}

  explicit Data(const Data& aOther)
    : mStrings(aOther.mStrings)
    , mVariables(aOther.mVariables)
    , mIndex(aOther.mIndex)
    , mBase(aOther.mBase)
    , mUseCompact(aOther.mUseCompact)
    , mIndexMask(aOther.mIndexMask)
    , mHash(0)
    , mFrozen(false)
    , mInCache(false)
    , mFactorFailure(NoFailure)
  {
    if (mUseCompact) {
      if (!mVariables.IsEmpty() && mIndex.IsEmpty()) {
        BuildIndex(8);
      }
    } else {
      RebuildLegacyIndex();
    }
  }

  ~Data()
  {
    if (mInCache) {
      MutexAutoLock lock(*sDataCacheMutex);
      void* cached = nullptr;
      if (sDataCache->Get(mHash, &cached) &&
          static_cast<Data*>(cached) == this) {
        sDataCache->Remove(mHash);
      }
    }
  }

  const char16_t* NameData(const Variable& aVariable) const
  {
    return mStrings.Elements() + aVariable.mNameOffset;
  }

  const char16_t* ValueData(const Variable& aVariable) const
  {
    return mStrings.Elements() + aVariable.mValueOffset;
  }

  nsDependentString Name(const Variable& aVariable) const
  {
    return nsDependentString(NameData(aVariable), aVariable.mNameLength);
  }

  nsDependentString Value(const Variable& aVariable) const
  {
    return nsDependentString(ValueData(aVariable), aVariable.mValueLength);
  }

  uint32_t AppendString(const nsAString& aString)
  {
    uint32_t offset = static_cast<uint32_t>(mStrings.Length());
    mStrings.AppendElements(aString.BeginReading(), aString.Length());
    return offset;
  }

  static uint32_t NameHash(const nsAString& aName)
  {
    uint32_t hash = 2166136261U;
    const char16_t* chars = aName.BeginReading();
    for (size_t i = 0, n = aName.Length(); i < n; i++) {
      hash ^= chars[i];
      hash *= 16777619U;
    }
    return hash;
  }

  bool NameEquals(const Variable& aVariable, const nsAString& aName) const
  {
    return aVariable.mNameLength == aName.Length() &&
           !memcmp(NameData(aVariable), aName.BeginReading(),
                   aVariable.mNameLength * sizeof(char16_t));
  }

  void InsertIndex(size_t aIndex)
  {
    MOZ_ASSERT(!mIndex.IsEmpty());
    uint32_t slot =
      NameHash(Name(mVariables[aIndex])) & mIndexMask;
    while (mIndex[slot]) {
      slot = (slot + 1) & mIndexMask;
    }
    mIndex[slot] = static_cast<uint32_t>(aIndex + 1);
  }

  void BuildIndex(uint32_t aCapacity)
  {
    if (aCapacity < 8) {
      aCapacity = 8;
    }
    uint32_t capacity = 1;
    while (capacity < aCapacity) {
      capacity <<= 1;
    }
    mIndex.Clear();
    mIndex.SetLength(capacity);
    memset(mIndex.Elements(), 0, capacity * sizeof(uint32_t));
    mIndexMask = capacity - 1;
    for (size_t i = 0, n = mVariables.Length(); i < n; i++) {
      InsertIndex(i);
    }
  }

  void RebuildLegacyIndex()
  {
    MOZ_ASSERT(!mUseCompact);
    if (!mLegacyIDs) {
      mLegacyIDs = new nsDataHashtable<nsStringHashKey, size_t>();
    } else {
      mLegacyIDs->Clear();
    }
    for (size_t i = 0, n = mVariables.Length(); i < n; i++) {
      nsString name;
      name.Assign(Name(mVariables[i]));
      mLegacyIDs->Put(name, i);
    }
  }

  bool HasBase() const { return !!mBase; }

  bool IsOverridden(size_t aBaseIndex, const Variable** aVariable) const
  {
    MOZ_ASSERT(mBase && !mBase->mBase);
    // Factoring visits the stable base prefix in order, then appended names
    // (UINT32_MAX). Search integer base positions without hashing long names
    // or linearly scanning a potentially large effective delta.
    size_t low = 0, high = mVariables.Length();
    while (low < high) {
      size_t middle = low + (high - low) / 2;
      if (mVariables[middle].mBaseIndex < aBaseIndex) {
        low = middle + 1;
      } else {
        high = middle;
      }
    }
    if (low < mVariables.Length() &&
        mVariables[low].mBaseIndex == aBaseIndex) {
      *aVariable = &mVariables[low];
      return true;
    }
    return false;
  }

  uint32_t Count() const
  {
    if (!mBase) {
      return mVariables.Length();
    }
    uint32_t count = mBase->Count();
    for (size_t i = 0, n = mVariables.Length(); i < n; i++) {
      if (mVariables[i].mBaseIndex == UINT32_MAX) {
        count++;
      }
    }
    return count;
  }

  bool Find(const nsAString& aName, size_t* aIndex) const
  {
    if (!mUseCompact) {
      return mLegacyIDs && mLegacyIDs->Get(aName, aIndex);
    }
    if (!mIndex.IsEmpty()) {
      uint32_t slot = NameHash(aName) & mIndexMask;
      while (uint32_t entry = mIndex[slot]) {
        size_t index = entry - 1;
        if (NameEquals(mVariables[index], aName)) {
          *aIndex = index;
          return true;
        }
        slot = (slot + 1) & mIndexMask;
      }
    }
    return false;
  }

  bool Contains(const nsAString& aName) const
  {
    size_t index;
    return Find(aName, &index) || (mBase && mBase->Find(aName, &index));
  }

  bool Get(const nsAString& aName, nsString& aValue,
           nsCSSTokenSerializationType& aFirstToken,
           nsCSSTokenSerializationType& aLastToken) const
  {
    size_t index;
    if (mUseCompact) {
      if (!mIndex.IsEmpty()) {
        uint32_t slot = NameHash(aName) & mIndexMask;
        while (uint32_t entry = mIndex[slot]) {
          size_t i = entry - 1;
          if (NameEquals(mVariables[i], aName)) {
            aValue.Assign(Value(mVariables[i]));
            aFirstToken = mVariables[i].FirstToken();
            aLastToken = mVariables[i].LastToken();
            return true;
          }
          slot = (slot + 1) & mIndexMask;
        }
      }
    } else if (mLegacyIDs && mLegacyIDs->Get(aName, &index)) {
      aValue.Assign(Value(mVariables[index]));
      aFirstToken = mVariables[index].FirstToken();
      aLastToken = mVariables[index].LastToken();
      return true;
    }
    return mBase && mBase->Get(aName, aValue, aFirstToken, aLastToken);
  }

  bool EntryAt(uint32_t aIndex, const Data** aOwner,
               const Variable** aEntry) const
  {
    if (mBase) {
      uint32_t baseCount = mBase->Count();
      if (aIndex < baseCount) {
        const Variable* override = nullptr;
        if (IsOverridden(aIndex, &override)) {
          *aOwner = this;
          *aEntry = override;
          return true;
        }
        return mBase->EntryAt(aIndex, aOwner, aEntry);
      }
      aIndex -= baseCount;
      for (size_t i = 0, n = mVariables.Length(); i < n; i++) {
        if (mVariables[i].mBaseIndex == UINT32_MAX) {
          if (aIndex == 0) {
            *aOwner = this;
            *aEntry = &mVariables[i];
            return true;
          }
          aIndex--;
        }
      }
      return false;
    }
    if (aIndex >= mVariables.Length()) {
      return false;
    }
    *aOwner = this;
    *aEntry = &mVariables[aIndex];
    return true;
  }

  bool GetAt(uint32_t aIndex, nsAString& aName, nsString& aValue,
             nsCSSTokenSerializationType& aFirstToken,
             nsCSSTokenSerializationType& aLastToken) const
  {
    const Data* owner = nullptr;
    const Variable* entry = nullptr;
    if (!EntryAt(aIndex, &owner, &entry)) {
      return false;
    }
    aName.Assign(owner->Name(*entry));
    aValue.Assign(owner->Value(*entry));
    aFirstToken = entry->FirstToken();
    aLastToken = entry->LastToken();
    return true;
  }

  already_AddRefed<Data> Flatten() const
  {
    if (!mBase) {
      RefPtr<Data> self = const_cast<Data*>(this);
      return self.forget();
    }
    RefPtr<Data> flat = new Data();
    flat->mUseCompact = mUseCompact;
    uint32_t count = Count();
    for (uint32_t i = 0; i < count; i++) {
      nsString name, value;
      nsCSSTokenSerializationType first = eCSSTokenSerialization_Nothing;
      nsCSSTokenSerializationType last = eCSSTokenSerialization_Nothing;
      MOZ_ALWAYS_TRUE(GetAt(i, name, value, first, last));
      flat->PutFull(name, value, first, last);
    }
    return flat.forget();
  }

  void PutFull(const nsAString& aName, const nsString& aValue,
               nsCSSTokenSerializationType aFirstToken,
               nsCSSTokenSerializationType aLastToken)
  {
    size_t id = mVariables.Length();
    if (mUseCompact) {
      if (mIndex.IsEmpty() || (id + 1) * 2 > mIndex.Length()) {
        BuildIndex(mIndex.IsEmpty() ? 8 : mIndex.Length() * 2);
      }
    }
    uint32_t nameOffset = AppendString(aName);
    uint32_t valueOffset = AppendString(aValue);
    mVariables.AppendElement(Variable(nameOffset, aName.Length(),
                                      valueOffset, aValue.Length(),
                                      Variable::PackTokens(aFirstToken,
                                                           aLastToken)));
    if (mUseCompact) {
      InsertIndex(id);
    } else {
      if (!mLegacyIDs) {
        mLegacyIDs = new nsDataHashtable<nsStringHashKey, size_t>();
      }
      nsString name(aName);
      mLegacyIDs->Put(name, id);
    }
  }

  bool Factor(const Data& aParent, uint32_t aMaxOverrides, bool aPersistent)
  {
    if (mBase) {
      mFactorFailure = OwnBase;
      return false;
    }
    // Retain the complete effective delta against the same flat ancestor,
    // rather than constructing a chain of inherited dictionary lookups.
    const Data& aBase = aPersistent && aParent.mBase
                      ? *aParent.mBase : aParent;
    if (aBase.mBase) {
      mFactorFailure = ParentBase;
      return false;
    }
    if (!aBase.mVariables.Length()) {
      mFactorFailure = EmptyBase;
      return false;
    }
    uint32_t baseCount = aBase.Count();
    uint32_t parentCount = aParent.Count();
    uint32_t fullCount = mVariables.Length();
    if (fullCount < parentCount) {
      mFactorFailure = FewerNames;
      return false;
    }
    RefPtr<Data> overrides = new Data();
    overrides->mUseCompact = mUseCompact;
    uint32_t parentOverrides = 0;
    for (uint32_t i = 0; i < fullCount; i++) {
      const Variable& variable = mVariables[i];
      if (i < parentCount) {
        const Data* owner = nullptr;
        const Variable* entry = nullptr;
        MOZ_ALWAYS_TRUE(aParent.EntryAt(i, &owner, &entry));
        if (variable.mNameLength != entry->mNameLength ||
            memcmp(NameData(variable), owner->NameData(*entry),
                   variable.mNameLength * sizeof(char16_t))) {
          mFactorFailure = DifferentOrder;
          return false;
        }
        if (variable.mValueLength != entry->mValueLength ||
            variable.mTokens != entry->mTokens ||
            memcmp(ValueData(variable), owner->ValueData(*entry),
                   variable.mValueLength * sizeof(char16_t))) {
          parentOverrides++;
        }
      } else {
        parentOverrides++;
      }
      // Keep the API's limit relative to the immediate parent. The stored
      // delta also includes inherited overrides against the flat ancestor.
      if (parentOverrides > aMaxOverrides) {
        mFactorFailure = TooManyOverrides;
        return false;
      }
      bool isBase = i < baseCount;
      if (isBase) {
        const Variable& base = aBase.mVariables[i];
        if (variable.mNameLength != base.mNameLength ||
            memcmp(NameData(variable), aBase.NameData(base),
                   variable.mNameLength * sizeof(char16_t))) {
          mFactorFailure = DifferentOrder;
          return false;
        }
        if (variable.mValueLength == base.mValueLength &&
            variable.mTokens == base.mTokens &&
            !memcmp(ValueData(variable), aBase.ValueData(base),
                    variable.mValueLength * sizeof(char16_t))) {
          continue;
        }
      }
      uint32_t nameOffset = overrides->AppendString(Name(variable));
      uint32_t valueOffset = overrides->AppendString(Value(variable));
      overrides->mVariables.AppendElement(
        Variable(nameOffset, variable.mNameLength, valueOffset, variable.mValueLength,
                 variable.mTokens,
                 isBase ? i : UINT32_MAX));
    }
    uint32_t overrideCount = overrides->mVariables.Length();
    if (aPersistent && overrideCount) {
      uint64_t oldBytes = uint64_t(mStrings.Length()) * sizeof(char16_t) +
                          uint64_t(mVariables.Length()) * (sizeof(Variable) + 8);
      uint64_t newBytes = uint64_t(overrides->mStrings.Length()) * sizeof(char16_t) +
                          uint64_t(overrides->mVariables.Length()) * (sizeof(Variable) + 8);
      if (newBytes * 4 > oldBytes * 3) {
        mFactorFailure = TooManyOverrides;
        return false;
      }
    }
    mFactorFailure = NoFailure;
    if (overrideCount == 0) {
      mBase = const_cast<Data*>(&aBase);
      mVariables.Clear();
      mStrings.Clear();
      mIndex.Clear();
      mLegacyIDs = nullptr;
      mIndexMask = 0;
      return true;
    }
    mBase = const_cast<Data*>(&aBase);
    mStrings.SwapElements(overrides->mStrings);
    mVariables.SwapElements(overrides->mVariables);
    mIndex.Clear();
    if (mUseCompact) {
      BuildIndex(mVariables.Length() * 2);
    } else {
      RebuildLegacyIndex();
    }
    return true;
  }

  void CompactStrings()
  {
    nsTArray<char16_t> compact;
    size_t total = 0;
    for (size_t i = 0, n = mVariables.Length(); i < n; i++) {
      total += mVariables[i].mNameLength + mVariables[i].mValueLength;
    }
    compact.SetCapacity(total);
    for (size_t i = 0, n = mVariables.Length(); i < n; i++) {
      Variable& variable = mVariables[i];
      uint32_t nameOffset = static_cast<uint32_t>(compact.Length());
      compact.AppendElements(NameData(variable), variable.mNameLength);
      variable.mNameOffset = nameOffset;
      uint32_t valueOffset = static_cast<uint32_t>(compact.Length());
      compact.AppendElements(ValueData(variable), variable.mValueLength);
      variable.mValueOffset = valueOffset;
    }
    mStrings.SwapElements(compact);
  }

  void Freeze()
  {
    if (mFrozen) {
      return;
    }
    CompactStrings();
    if (mUseCompact && !mVariables.IsEmpty() && mIndex.IsEmpty()) {
      BuildIndex(8);
    }
    mFrozen = true;
  }

  static void Mix(uint64_t& aHash, const char16_t* aData, size_t aLength)
  {
    for (size_t i = 0; i < aLength; i++) {
      aHash ^= aData[i];
      aHash *= 1099511628211ULL;
    }
  }

  static void Mix(uint64_t& aHash, uint64_t aValue)
  {
    for (uint32_t i = 0; i < 8; i++) {
      aHash ^= (aValue >> (i * 8)) & 0xff;
      aHash *= 1099511628211ULL;
    }
  }

  uint64_t Hash() const
  {
    uint64_t hash = 1469598103934665603ULL;
    uint32_t count = Count();
    Mix(hash, static_cast<uint64_t>(count));
    for (uint32_t i = 0; i < count; i++) {
      const Data* owner = nullptr;
      const Variable* entry = nullptr;
      MOZ_ALWAYS_TRUE(EntryAt(i, &owner, &entry));
      const Variable& variable = *entry;
      Mix(hash, static_cast<uint64_t>(variable.mNameLength));
      Mix(hash, owner->NameData(variable), variable.mNameLength);
      Mix(hash, static_cast<uint64_t>(variable.mValueLength));
      Mix(hash, owner->ValueData(variable), variable.mValueLength);
      Mix(hash, static_cast<uint64_t>(variable.mTokens));
    }
    return hash;
  }

  bool EqualsInOrder(const Data& aOther) const
  {
    uint32_t count = Count();
    if (count != aOther.Count()) {
      return false;
    }
    for (uint32_t i = 0; i < count; i++) {
      const Data* leftOwner = nullptr;
      const Data* rightOwner = nullptr;
      const Variable* leftEntry = nullptr;
      const Variable* rightEntry = nullptr;
      MOZ_ALWAYS_TRUE(EntryAt(i, &leftOwner, &leftEntry));
      MOZ_ALWAYS_TRUE(aOther.EntryAt(i, &rightOwner, &rightEntry));
      const Variable& left = *leftEntry;
      const Variable& right = *rightEntry;
      if (left.mNameLength != right.mNameLength ||
          left.mValueLength != right.mValueLength ||
          left.mTokens != right.mTokens ||
          memcmp(leftOwner->NameData(left), rightOwner->NameData(right),
                 left.mNameLength * sizeof(char16_t)) ||
          memcmp(leftOwner->ValueData(left), rightOwner->ValueData(right),
                 left.mValueLength * sizeof(char16_t))) {
        return false;
      }
    }
    return true;
  }

  MemoryUsage MeasureMemory(MallocSizeOf aMallocSizeOf) const
  {
    MemoryUsage usage;
    usage.mMetadata = aMallocSizeOf(this);
    usage.mStrings = mStrings.ShallowSizeOfExcludingThis(aMallocSizeOf);
    usage.mRecords = mVariables.ShallowSizeOfExcludingThis(aMallocSizeOf);
    usage.mIndexes = mIndex.ShallowSizeOfExcludingThis(aMallocSizeOf);
    if (mLegacyIDs) {
      usage.mIndexes += mLegacyIDs->ShallowSizeOfExcludingThis(aMallocSizeOf);
      for (auto iter = mLegacyIDs->ConstIter(); !iter.Done(); iter.Next()) {
        usage.mIndexes += iter.Key().SizeOfExcludingThisIfUnshared(aMallocSizeOf);
      }
    }
    usage.mTotal = usage.mMetadata + usage.mStrings + usage.mRecords + usage.mIndexes;
    return usage;
  }

  nsTArray<char16_t> mStrings;
  nsTArray<Variable> mVariables;
  nsTArray<uint32_t> mIndex;
  RefPtr<Data> mBase;
  nsAutoPtr<nsDataHashtable<nsStringHashKey, size_t> > mLegacyIDs;
  bool mUseCompact;
  uint32_t mIndexMask;
  uint64_t mHash;
  bool mFrozen;
  bool mInCache;
  uint8_t mFactorFailure;
};

CSSVariableValues::CSSVariableValues()
{
  MOZ_COUNT_CTOR(CSSVariableValues);
  RegisterVariableValues(this);
}

CSSVariableValues::CSSVariableValues(const CSSVariableValues& aOther)
  : mData(aOther.mData)
{
  MOZ_COUNT_CTOR(CSSVariableValues);
  RegisterVariableValues(this);
}

CSSVariableValues::~CSSVariableValues()
{
  UnregisterVariableValues(this);
  MOZ_COUNT_DTOR(CSSVariableValues);
}

CSSVariableValues&
CSSVariableValues::operator=(const CSSVariableValues& aOther)
{
  if (this != &aOther) {
    mData = aOther.mData;
  }
  return *this;
}

CSSVariableValues::Data*
CSSVariableValues::EnsureWritableData()
{
  MOZ_ASSERT(NS_IsMainThread());
  if (mData && mData->HasBase()) {
    mData = mData->Flatten();
  }
  if (!mData) {
    mData = new Data();
  } else if (!mData->hasOneRef() || mData->mFrozen || mData->mInCache) {
    mData = new Data(*mData);
  }
  return mData;
}

bool
CSSVariableValues::operator==(const CSSVariableValues& aOther) const
{
  if (mData == aOther.mData) {
    return true;
  }
  if (!mData || !aOther.mData) {
    return false;
  }
  if (mData->Count() != aOther.mData->Count()) {
    return false;
  }

  for (size_t i = 0, n = mData->Count(); i < n; ++i) {
    nsString name, value;
    nsCSSTokenSerializationType first = eCSSTokenSerialization_Nothing;
    nsCSSTokenSerializationType last = eCSSTokenSerialization_Nothing;
    MOZ_ALWAYS_TRUE(mData->GetAt(i, name, value, first, last));
    nsString otherValue;
    nsCSSTokenSerializationType otherFirst, otherLast;
    if (!aOther.mData->Get(name, otherValue, otherFirst, otherLast) ||
        !value.Equals(otherValue)) {
      return false;
    }
  }

  return true;
}

bool
CSSVariableValues::EqualsInOrder(const CSSVariableValues& aOther) const
{
  if (mData == aOther.mData) {
    return true;
  }
  if (!mData || !aOther.mData) {
    return !mData && !aOther.mData;
  }
  return mData->EqualsInOrder(*aOther.mData);
}

size_t
CSSVariableValues::Count() const
{
  return mData ? mData->Count() : 0;
}

bool
CSSVariableValues::Get(const nsAString& aName, nsString& aValue) const
{
  if (!mData) {
    return false;
  }
  nsCSSTokenSerializationType first, last;
  return mData->Get(aName, aValue, first, last);
}

bool
CSSVariableValues::Get(const nsAString& aName,
                       nsString& aValue,
                       nsCSSTokenSerializationType& aFirstToken,
                       nsCSSTokenSerializationType& aLastToken) const
{
  if (!mData) {
    return false;
  }
  return mData->Get(aName, aValue, aFirstToken, aLastToken);
}

void
CSSVariableValues::GetVariableAt(size_t aIndex, nsAString& aName) const
{
  MOZ_ASSERT(mData);
  nsString value;
  nsCSSTokenSerializationType first, last;
  MOZ_ALWAYS_TRUE(mData->GetAt(aIndex, aName, value, first, last));
}

void
CSSVariableValues::Put(const nsAString& aName,
                       nsString aValue,
                       nsCSSTokenSerializationType aFirstToken,
                       nsCSSTokenSerializationType aLastToken)
{
  Data* data = EnsureWritableData();
  size_t id;
  if (data->Find(aName, &id)) {
    Data::Variable& variable = data->mVariables[id];
    variable.mValueOffset = data->AppendString(aValue);
    variable.mValueLength = static_cast<uint32_t>(aValue.Length());
    variable.mTokens = Data::Variable::PackTokens(aFirstToken, aLastToken);
  } else {
    id = data->mVariables.Length();
    if (data->mUseCompact) {
      if (data->mIndex.IsEmpty() ||
          (id + 1) * 2 > data->mIndex.Length()) {
        data->BuildIndex(data->mIndex.IsEmpty() ? 8 :
                           data->mIndex.Length() * 2);
      }
    }
    uint32_t nameOffset = data->AppendString(aName);
    uint32_t valueOffset = data->AppendString(aValue);
    data->mVariables.AppendElement(
      Data::Variable(nameOffset, aName.Length(), valueOffset,
                     aValue.Length(),
                     Data::Variable::PackTokens(aFirstToken, aLastToken)));
    if (data->mUseCompact) {
      data->InsertIndex(id);
    } else {
      if (!data->mLegacyIDs) {
        data->mLegacyIDs = new nsDataHashtable<nsStringHashKey, size_t>();
      }
      nsString name;
      name.Assign(aName);
      data->mLegacyIDs->Put(name, id);
    }
  }
}

void
CSSVariableValues::Remove(const nsAString& aName)
{
  if (!mData || !mData->Contains(aName)) {
    return;
  }

  // A local overlay index is not its index after flattening. Detach first,
  // then find the entry in the writable dictionary's enumeration order.
  Data* data = EnsureWritableData();
  size_t id;
  if (!data->Find(aName, &id)) {
    return;
  }
  data->mVariables.RemoveElementAt(id);
  if (data->mVariables.IsEmpty()) {
    mData = nullptr;
    return;
  }
  if (data->mUseCompact) {
    data->BuildIndex(data->mIndex.Length());
  } else {
    data->RebuildLegacyIndex();
  }
}

bool
CSSVariableValues::Factor(const CSSVariableValues& aBase, uint32_t aMaxOverrides)
{
  if (!mData || !aBase.mData) {
    return false;
  }
  if (mData == aBase.mData) {
    return true;
  }
  int32_t tier = Preferences::GetInt("browser.memoryTier", 3);
  bool persistent = Preferences::GetBool("layout.css.variables.persistent",
                                       tier >= 0 && tier <= 1);
  if (persistent && aBase.mData->HasBase() &&
      mData->EqualsInOrder(*aBase.mData)) {
    mData = aBase.mData;
    return true;
  }
  // Factor mutates the representation, even though its computed values stay
  // identical. Shared, frozen and interned dictionaries must detach first.
  return EnsureWritableData()->Factor(*aBase.mData, aMaxOverrides, persistent);
}

void
CSSVariableValues::Intern()
{
  MOZ_ASSERT(NS_IsMainThread());
  if (!mData || mData->mVariables.IsEmpty() || mData->mInCache) {
    return;
  }

  // Shared mutable data must detach before compaction changes its offsets.
  // Already-frozen data needs no copy, including factored sets outside the
  // weak whole-dictionary cache.
  if (!mData->mFrozen) {
    if (!mData->hasOneRef()) {
      mData = new Data(*mData);
    }
    mData->Freeze();
  }
  if (mData->HasBase() ||
      !Preferences::GetBool("layout.css.variables.intern", true)) {
    return;
  }

  uint64_t hash = mData->Hash();
  MutexAutoLock lock(*sDataCacheMutex);
  void* cached = nullptr;
  if (sDataCache->Get(hash, &cached)) {
    Data* other = static_cast<Data*>(cached);
    // A hash collision is not a correctness problem: just leave this set
    // uninterned rather than sharing unequal data.
    if (other->EqualsInOrder(*mData)) {
      mData = other;
    }
    return;
  }

  mData->mHash = hash;
  mData->mInCache = true;
  sDataCache->Put(hash, mData);
}

void
CSSVariableValues::AddVariablesToResolver(CSSVariableResolver* aResolver) const
{
  if (!mData) {
    return;
  }
  for (size_t i = 0, n = mData->Count(); i < n; i++) {
    nsString name, value;
    nsCSSTokenSerializationType first = eCSSTokenSerialization_Nothing;
    nsCSSTokenSerializationType last = eCSSTokenSerialization_Nothing;
    MOZ_ALWAYS_TRUE(mData->GetAt(i, name, value, first, last));
    aResolver->Put(name, value, first, last, true);
  }
}

/* static */ size_t
CSSVariableValues::SizeOfLive(MallocSizeOf aMallocSizeOf)
{
  return MeasureMemory(aMallocSizeOf).mTotal;
}

CSSVariableValues::MemoryUsage
CSSVariableValues::MeasureMemory(MallocSizeOf aMallocSizeOf)
{
  MemoryUsage usage;
  nsTHashtable<nsPtrHashKey<Data> > seen;
  MutexAutoLock lock(*sLiveValuesMutex);
  usage.mReferences = sLiveValues->Length();
  for (size_t i = 0, len = sLiveValues->Length(); i < len; i++) {
    Data* data = (*sLiveValues)[i]->mData;
    while (data && !seen.GetEntry(data)) {
      seen.PutEntry(data);
      // Invoke the size-of callback once per allocation. DMD treats a second
      // callback as another report of that allocation, even for alternate views.
      MemoryUsage local = data->MeasureMemory(aMallocSizeOf);
      size_t bytes = local.mTotal;
      usage.mTotal += bytes;
      if (data->mBase) {
        usage.mFactored += bytes;
        usage.mFactoredBlocks++;
      } else {
        usage.mFlat += bytes;
        usage.mFlatBlocks++;
        usage.mRejected[data->mFactorFailure] += bytes;
      }
      usage.mLocalVariables += data->mVariables.Length();
      usage.mStrings += local.mStrings;
      usage.mRecords += local.mRecords;
      usage.mIndexes += local.mIndexes;
      usage.mMetadata += local.mMetadata;
      data = data->mBase;
    }
  }
  return usage;
}

} // namespace mozilla
