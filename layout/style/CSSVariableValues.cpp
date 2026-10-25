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
             uint32_t aTokens)
      : mNameOffset(aNameOffset)
      , mNameLength(aNameLength)
      , mValueOffset(aValueOffset)
      , mValueLength(aValueLength)
      , mTokens(aTokens)
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

  bool Find(const nsAString& aName, size_t* aIndex) const
  {
    if (!mUseCompact) {
      return mLegacyIDs && mLegacyIDs->Get(aName, aIndex);
    }
    if (mIndex.IsEmpty()) {
      return false;
    }
    uint32_t slot = NameHash(aName) & mIndexMask;
    while (uint32_t entry = mIndex[slot]) {
      size_t index = entry - 1;
      if (NameEquals(mVariables[index], aName)) {
        *aIndex = index;
        return true;
      }
      slot = (slot + 1) & mIndexMask;
    }
    return false;
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
  if (mData->mVariables.Length() != aOther.mData->mVariables.Length()) {
    return false;
  }

  for (size_t thisIndex = 0; thisIndex < mData->mVariables.Length();
       ++thisIndex) {
    size_t otherIndex;
    if (!aOther.mData->Find(mData->Name(mData->mVariables[thisIndex]),
                            &otherIndex)) {
      return false;
    }
    if (!mData->Value(mData->mVariables[thisIndex])
           .Equals(aOther.mData->Value(aOther.mData->mVariables[otherIndex]))) {
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
  return mData ? mData->mVariables.Length() : 0;
}

bool
CSSVariableValues::Get(const nsAString& aName, nsString& aValue) const
{
  if (!mData) {
    return false;
  }
  size_t id;
  if (!mData->Find(aName, &id)) {
    return false;
  }
  aValue.Assign(mData->Value(mData->mVariables[id]));
  return true;
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
  size_t id;
  if (!mData->Find(aName, &id)) {
    return false;
  }
  const Data::Variable& variable = mData->mVariables[id];
  aValue.Assign(mData->Value(variable));
  aFirstToken = variable.FirstToken();
  aLastToken = variable.LastToken();
  return true;
}

void
CSSVariableValues::GetVariableAt(size_t aIndex, nsAString& aName) const
{
  MOZ_ASSERT(mData && aIndex < mData->mVariables.Length());
  aName.Assign(mData->Name(mData->mVariables[aIndex]));
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
  if (!Preferences::GetBool("layout.css.variables.intern", true)) {
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
  for (size_t i = 0, n = mData->mVariables.Length(); i < n; i++) {
    const Data::Variable& variable = mData->mVariables[i];
    nsString value;
    value.Assign(mData->Value(variable));
    aResolver->Put(mData->Name(variable), value,
                   variable.FirstToken(), variable.LastToken(), true);
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
    if (data && !seen.GetEntry(data)) {
      seen.PutEntry(data);
      n += data->SizeOfExcludingThis(aMallocSizeOf);
    }
  }
  return n;
}

} // namespace mozilla
