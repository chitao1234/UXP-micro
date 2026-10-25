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
#include "nsTHashtable.h"
#include "nsHashKeys.h"

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
    Variable()
      : mFirstToken(eCSSTokenSerialization_Nothing)
      , mLastToken(eCSSTokenSerialization_Nothing)
    {}

    Variable(const nsAString& aVariableName,
             nsString aValue,
             nsCSSTokenSerializationType aFirstToken,
             nsCSSTokenSerializationType aLastToken)
      : mVariableName(aVariableName)
      , mValue(aValue)
      , mFirstToken(aFirstToken)
      , mLastToken(aLastToken)
    {}

    nsString mVariableName;
    nsString mValue;
    nsCSSTokenSerializationType mFirstToken;
    nsCSSTokenSerializationType mLastToken;
  };

  Data()
    : mHash(0)
    , mInCache(false)
  {}

  explicit Data(const Data& aOther)
    : mVariables(aOther.mVariables)
    , mHash(0)
    , mInCache(false)
  {
    for (auto iter = aOther.mVariableIDs.ConstIter(); !iter.Done(); iter.Next()) {
      mVariableIDs.Put(iter.Key(), iter.UserData());
    }
  }

  ~Data()
  {
    if (mInCache) {
      MutexAutoLock lock(*sDataCacheMutex);
      void* cached = nullptr;
      if (sDataCache->Get(mHash, &cached) && static_cast<Data*>(cached) == this) {
        sDataCache->Remove(mHash);
      }
    }
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
      Mix(hash, static_cast<uint64_t>(variable.mVariableName.Length()));
      Mix(hash, variable.mVariableName.BeginReading(),
          variable.mVariableName.Length());
      Mix(hash, static_cast<uint64_t>(variable.mValue.Length()));
      Mix(hash, variable.mValue.BeginReading(), variable.mValue.Length());
      Mix(hash, static_cast<uint64_t>(variable.mFirstToken));
      Mix(hash, static_cast<uint64_t>(variable.mLastToken));
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
      if (!left.mVariableName.Equals(right.mVariableName) ||
          !left.mValue.Equals(right.mValue) ||
          left.mFirstToken != right.mFirstToken ||
          left.mLastToken != right.mLastToken) {
        return false;
      }
    }
    return true;
  }

  size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const
  {
    size_t n = aMallocSizeOf(this);
    n += mVariables.ShallowSizeOfExcludingThis(aMallocSizeOf);
    for (size_t i = 0, len = mVariables.Length(); i < len; i++) {
      n += mVariables[i].mVariableName.SizeOfExcludingThisIfUnshared(
        aMallocSizeOf);
      n += mVariables[i].mValue.SizeOfExcludingThisIfUnshared(aMallocSizeOf);
    }

    n += mVariableIDs.ShallowSizeOfExcludingThis(aMallocSizeOf);
    for (auto iter = mVariableIDs.ConstIter(); !iter.Done(); iter.Next()) {
      n += iter.Key().SizeOfExcludingThisIfUnshared(aMallocSizeOf);
    }
    return n;
  }

  nsTArray<Variable> mVariables;
  nsDataHashtable<nsStringHashKey, size_t> mVariableIDs;
  uint64_t mHash;
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
  if (!mData) {
    mData = new Data();
  } else if (!mData->hasOneRef()) {
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
    if (!aOther.mData->mVariableIDs.Get(
          mData->mVariables[thisIndex].mVariableName, &otherIndex)) {
      return false;
    }
    if (!mData->mVariables[thisIndex].mValue.Equals(
          aOther.mData->mVariables[otherIndex].mValue)) {
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
  if (mData->mVariables.Length() != aOther.mData->mVariables.Length()) {
    return false;
  }

  for (size_t i = 0, n = mData->mVariables.Length(); i < n; ++i) {
    const Data::Variable& left = mData->mVariables[i];
    const Data::Variable& right = aOther.mData->mVariables[i];
    if (!left.mVariableName.Equals(right.mVariableName) ||
        !left.mValue.Equals(right.mValue) ||
        left.mFirstToken != right.mFirstToken ||
        left.mLastToken != right.mLastToken) {
      return false;
    }
  }
  return true;
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
  if (!mData->mVariableIDs.Get(aName, &id)) {
    return false;
  }
  aValue = mData->mVariables[id].mValue;
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
  if (!mData->mVariableIDs.Get(aName, &id)) {
    return false;
  }
  aValue = mData->mVariables[id].mValue;
  aFirstToken = mData->mVariables[id].mFirstToken;
  aLastToken = mData->mVariables[id].mLastToken;
  return true;
}

void
CSSVariableValues::GetVariableAt(size_t aIndex, nsAString& aName) const
{
  MOZ_ASSERT(mData && aIndex < mData->mVariables.Length());
  aName = mData->mVariables[aIndex].mVariableName;
}

void
CSSVariableValues::Put(const nsAString& aName,
                       nsString aValue,
                       nsCSSTokenSerializationType aFirstToken,
                       nsCSSTokenSerializationType aLastToken)
{
  Data* data = EnsureWritableData();
  size_t id;
  if (data->mVariableIDs.Get(aName, &id)) {
    data->mVariables[id].mValue = aValue;
    data->mVariables[id].mFirstToken = aFirstToken;
    data->mVariables[id].mLastToken = aLastToken;
  } else {
    id = data->mVariables.Length();
    data->mVariableIDs.Put(aName, id);
    data->mVariables.AppendElement(Data::Variable(aName, aValue,
                                                  aFirstToken, aLastToken));
  }
}

void
CSSVariableValues::Remove(const nsAString& aName)
{
  if (!mData) {
    return;
  }
  size_t id;
  if (!mData->mVariableIDs.Get(aName, &id)) {
    return;
  }

  Data* data = EnsureWritableData();
  data->mVariables.RemoveElementAt(id);
  data->mVariableIDs.Clear();
  for (size_t i = 0, n = data->mVariables.Length(); i < n; i++) {
    data->mVariableIDs.Put(data->mVariables[i].mVariableName, i);
  }
  if (data->mVariables.IsEmpty()) {
    mData = nullptr;
  }
}

void
CSSVariableValues::Intern()
{
  if (!mData || mData->mVariables.IsEmpty() || mData->mInCache) {
    return;
  }
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
    aResolver->Put(mData->mVariables[i].mVariableName,
                   mData->mVariables[i].mValue,
                   mData->mVariables[i].mFirstToken,
                   mData->mVariables[i].mLastToken,
                   true);
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
