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
    for (size_t i = 0, n = mVariables.Length(); i < n; i++) {
      if (mVariables[i].mBaseIndex == aBaseIndex) {
        *aVariable = &mVariables[i];
        return true;
      }
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
    return mBase && mBase->Find(aName, aIndex);
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

  bool GetAt(uint32_t aIndex, nsAString& aName, nsString& aValue,
             nsCSSTokenSerializationType& aFirstToken,
             nsCSSTokenSerializationType& aLastToken) const
  {
    if (mBase) {
      uint32_t baseCount = mBase->Count();
      if (aIndex < baseCount) {
        const Variable* override = nullptr;
        if (IsOverridden(aIndex, &override)) {
          aName.Assign(Name(*override));
          aValue.Assign(Value(*override));
          aFirstToken = override->FirstToken();
          aLastToken = override->LastToken();
          return true;
        }
        return mBase->GetAt(aIndex, aName, aValue, aFirstToken, aLastToken);
      }
      aIndex -= baseCount;
      for (size_t i = 0, n = mVariables.Length(); i < n; i++) {
        if (mVariables[i].mBaseIndex == UINT32_MAX) {
          if (aIndex == 0) {
            aName.Assign(Name(mVariables[i]));
            aValue.Assign(Value(mVariables[i]));
            aFirstToken = mVariables[i].FirstToken();
            aLastToken = mVariables[i].LastToken();
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
    const Variable& v = mVariables[aIndex];
    aName.Assign(Name(v));
    aValue.Assign(Value(v));
    aFirstToken = v.FirstToken();
    aLastToken = v.LastToken();
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
      nsCSSTokenSerializationType first, last;
      GetAt(i, name, value, first, last);
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
    }
  }

  bool Factor(const Data& aBase, uint32_t aMaxOverrides)
  {
    if (mBase || aBase.mBase || !aBase.mVariables.Length()) {
      return false;
    }
    uint32_t baseCount = aBase.Count();
    uint32_t fullCount = mVariables.Length();
    if (fullCount < baseCount) {
      return false;
    }
    RefPtr<Data> overrides = new Data();
    overrides->mUseCompact = mUseCompact;
    uint32_t overrideCount = 0;
    for (uint32_t i = 0; i < fullCount; i++) {
      nsString name, value;
      nsCSSTokenSerializationType first, last;
      GetAt(i, name, value, first, last);
      bool isBase = i < baseCount;
      if (isBase) {
        nsString baseName, baseValue;
        nsCSSTokenSerializationType baseFirst, baseLast;
        aBase.GetAt(i, baseName, baseValue, baseFirst, baseLast);
        if (!name.Equals(baseName)) {
          return false;
        }
        if (value.Equals(baseValue) && first == baseFirst && last == baseLast) {
          continue;
        }
      }
      uint32_t nameOffset = overrides->AppendString(name);
      uint32_t valueOffset = overrides->AppendString(value);
      overrides->mVariables.AppendElement(
        Variable(nameOffset, name.Length(), valueOffset, value.Length(),
                 Variable::PackTokens(first, last),
                 isBase ? i : UINT32_MAX));
      overrideCount++;
      if (overrideCount > aMaxOverrides) {
        return false;
      }
    }
    if (overrideCount == 0) {
      mBase = const_cast<Data*>(&aBase);
      mVariables.Clear();
      mStrings.Clear();
      mIndex.Clear();
      mIndexMask = 0;
      return true;
    }
    mBase = const_cast<Data*>(&aBase);
    mStrings.SwapElements(overrides->mStrings);
    mVariables.SwapElements(overrides->mVariables);
    mIndex.Clear();
    BuildIndex(mVariables.Length() + 8);
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
    if (mBase) {
      RefPtr<Data> flat = Flatten();
      return flat->Hash();
    }
    uint64_t hash = 1469598103934665603ULL;
    Mix(hash, static_cast<uint64_t>(mVariables.Length()));
    for (size_t i = 0, n = mVariables.Length(); i < n; i++) {
      const Variable& variable = mVariables[i];
      Mix(hash, static_cast<uint64_t>(variable.mNameLength));
      Mix(hash, NameData(variable), variable.mNameLength);
      Mix(hash, static_cast<uint64_t>(variable.mValueLength));
      Mix(hash, ValueData(variable), variable.mValueLength);
      Mix(hash, static_cast<uint64_t>(variable.mTokens));
    }
    return hash;
  }

  bool EqualsInOrder(const Data& aOther) const
  {
    if (mBase || aOther.mBase) {
      RefPtr<Data> left = Flatten();
      RefPtr<Data> right = const_cast<Data&>(aOther).Flatten();
      return left->EqualsInOrder(*right);
    }
    if (mVariables.Length() != aOther.mVariables.Length()) {
      return false;
    }
    for (size_t i = 0, n = mVariables.Length(); i < n; i++) {
      const Variable& left = mVariables[i];
      const Variable& right = aOther.mVariables[i];
      if (left.mNameLength != right.mNameLength ||
          left.mValueLength != right.mValueLength ||
          left.mTokens != right.mTokens ||
          memcmp(NameData(left), aOther.NameData(right),
                 left.mNameLength * sizeof(char16_t)) ||
          memcmp(ValueData(left), aOther.ValueData(right),
                 left.mValueLength * sizeof(char16_t))) {
        return false;
      }
    }
    return true;
  }

  size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const
  {
    size_t n = aMallocSizeOf(this);
    n += mStrings.ShallowSizeOfExcludingThis(aMallocSizeOf);
    n += mVariables.ShallowSizeOfExcludingThis(aMallocSizeOf);
    n += mIndex.ShallowSizeOfExcludingThis(aMallocSizeOf);
    if (mLegacyIDs) {
      n += mLegacyIDs->ShallowSizeOfExcludingThis(aMallocSizeOf);
      for (auto iter = mLegacyIDs->ConstIter(); !iter.Done(); iter.Next()) {
        n += iter.Key().SizeOfExcludingThisIfUnshared(aMallocSizeOf);
      }
    }
    return n;
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

  for (size_t i = 0; i < mData->Count(); ++i) {
    nsString name, value;
    nsCSSTokenSerializationType first, last;
    mData->GetAt(i, name, value, first, last);
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
  if (!mData) {
    return;
  }
  size_t id;
  if (!mData->Find(aName, &id)) {
    return;
  }

  Data* data = EnsureWritableData();
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
  return mData->Factor(*aBase.mData, aMaxOverrides);
}

void
CSSVariableValues::Intern()
{
  MOZ_ASSERT(NS_IsMainThread());
  if (!mData || mData->mVariables.IsEmpty() || mData->mInCache) {
    return;
  }

  // Freeze only a block that this object exclusively owns.  A shared mutable
  // block must be detached before compaction changes its offsets.
  if (!mData->hasOneRef()) {
    mData = new Data(*mData);
  }
  mData->Freeze();
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
    nsCSSTokenSerializationType first, last;
    mData->GetAt(i, name, value, first, last);
    aResolver->Put(name, value, first, last, true);
  }
}

/* static */ size_t
CSSVariableValues::SizeOfLive(MallocSizeOf aMallocSizeOf)
{
  size_t n = 0;
  nsTHashtable<nsPtrHashKey<Data> > seen;
  MutexAutoLock lock(*sLiveValuesMutex);
  for (size_t i = 0, len = sLiveValues->Length(); i < len; i++) {
    Data* data = (*sLiveValues)[i]->mData;
    while (data && !seen.GetEntry(data)) {
      seen.PutEntry(data);
      n += data->SizeOfExcludingThis(aMallocSizeOf);
      data = data->mBase;
    }
  }
  return n;
}

} // namespace mozilla
