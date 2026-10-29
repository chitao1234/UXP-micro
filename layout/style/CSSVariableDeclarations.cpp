/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* CSS Custom Property assignments for a Declaration at a given priority */

#include "CSSVariableDeclarations.h"

#include "CSSVariableResolver.h"
#include "CSSVariableValues.h"
#include "nsCSSScanner.h"
#include "mozilla/LinkedList.h"
#include "mozilla/Preferences.h"
#include "mozilla/RefCounted.h"
#include "nsHashKeys.h"
#include "nsThreadUtils.h"
#include <cstring>
#include "nsRuleData.h"

// These special string values are used to represent specified values of
// CSS-wide keywords.  (Note that none of these are valid variable values.)
#define INITIAL_VALUE "!"
#define INHERIT_VALUE ";"
#define UNSET_VALUE   ")"
#define REVERT_VALUE  ">"
#define REVERT_LAYER_VALUE  "]"

static const int32_t kRevertLayerMask = 0x000fffff;
static const int32_t kRevertLayerImportant = 0x00100000;
static const int32_t kRevertLayerOriginShift = 21;
static const int32_t kLayerIndexMax = 0x000fffff;
static const int32_t kCascadeRankStride = kLayerIndexMax + 1;

namespace mozilla {

static bool
IsRevertLayerValue(const nsString& aValue)
{
  return StringBeginsWith(aValue, NS_LITERAL_STRING(REVERT_LAYER_VALUE));
}

static bool
ValueMightResolveToRevertLayer(const nsString& aValue)
{
  return IsRevertLayerValue(aValue) ||
         aValue.Find(NS_LITERAL_STRING("revert-layer")) != kNotFound;
}

static int32_t
PackRevertLayerData(SheetType aLevel, bool aIsImportant,
                    int32_t aLayerIndex)
{
  MOZ_ASSERT(aLayerIndex >= 0 && aLayerIndex <= kRevertLayerMask);
  return (uint32_t(aLevel) << kRevertLayerOriginShift) |
         (aIsImportant ? kRevertLayerImportant : 0) |
         (aLayerIndex & kRevertLayerMask);
}

static void
SetRevertLayerData(nsString& aValue, const nsRuleData* aRuleData)
{
  aValue.AssignLiteral(REVERT_LAYER_VALUE);
  aValue.AppendInt(PackRevertLayerData(aRuleData->mLevel,
                                       aRuleData->mIsImportantRule,
                                       aRuleData->mLayerIndex));
}

static bool
UnpackRevertLayerData(const nsString& aValue, SheetType& aLevel,
                      bool& aIsImportant, int32_t& aLayerIndex)
{
  if (!IsRevertLayerValue(aValue) ||
      aValue.Length() == NS_LITERAL_STRING(REVERT_LAYER_VALUE).Length()) {
    return false;
  }

  nsresult rv;
  nsAutoString metadata;
  metadata.Assign(Substring(aValue,
                            NS_LITERAL_STRING(REVERT_LAYER_VALUE).Length()));
  int32_t packed = metadata.ToInteger(&rv);
  if (NS_FAILED(rv)) {
    return false;
  }

  aLevel = SheetType(uint32_t(packed) >> kRevertLayerOriginShift);
  aIsImportant = (packed & kRevertLayerImportant) != 0;
  aLayerIndex = packed & kRevertLayerMask;
  return true;
}

static int32_t
LayerIndexRank(bool aIsImportant, int32_t aLayerIndex)
{
  MOZ_ASSERT(aLayerIndex >= 0);
  MOZ_ASSERT(aLayerIndex <= kLayerIndexMax);
  return aIsImportant ? kLayerIndexMax - aLayerIndex : aLayerIndex;
}

static int32_t
CascadePrecedenceRank(SheetType aLevel, bool aIsImportant,
                      int32_t aLayerIndex)
{
  int32_t bucket = 0;
  bool isLayeredOrigin = false;

  switch (aLevel) {
    case SheetType::Agent:
      bucket = aIsImportant ? 14 : 0;
      isLayeredOrigin = true;
      break;
    case SheetType::User:
      bucket = aIsImportant ? 13 : 1;
      isLayeredOrigin = true;
      break;
    case SheetType::PresHint:
      bucket = 2;
      break;
    case SheetType::SVGAttrAnimation:
      bucket = 3;
      break;
    case SheetType::Doc:
      bucket = aIsImportant ? 10 : 4;
      isLayeredOrigin = true;
      break;
    case SheetType::ScopedDoc:
      bucket = aIsImportant ? 9 : 5;
      isLayeredOrigin = true;
      break;
    case SheetType::StyleAttr:
      bucket = aIsImportant ? 11 : 6;
      break;
    case SheetType::Override:
      bucket = aIsImportant ? 12 : 7;
      break;
    case SheetType::Animation:
      bucket = 8;
      break;
    case SheetType::Transition:
      bucket = 15;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("unexpected cascade level");
      break;
  }

  return bucket * kCascadeRankStride +
         (isLayeredOrigin ? LayerIndexRank(aIsImportant, aLayerIndex) : 0);
}

static bool
ShouldIgnoreForRevertLayerData(SheetType aSourceLevel,
                               bool aSourceImportant,
                               int32_t aSourceLayerIndex,
                               SheetType aCandidateLevel,
                               bool aCandidateImportant,
                               int32_t aCandidateLayerIndex)
{
  int32_t candidateRank =
    CascadePrecedenceRank(aCandidateLevel, aCandidateImportant,
                          aCandidateLayerIndex);
  int32_t sourceRank =
    CascadePrecedenceRank(aSourceLevel, aSourceImportant, aSourceLayerIndex);

  if (candidateRank >= sourceRank) {
    return true;
  }

  if (!aSourceImportant) {
    return false;
  }

  if (aSourceLevel == SheetType::StyleAttr) {
    return aCandidateLevel == SheetType::StyleAttr ||
           aCandidateLevel == SheetType::Animation;
  }

  int32_t normalBoundary =
    CascadePrecedenceRank(aSourceLevel, false, aSourceLayerIndex);
  return candidateRank >= normalBoundary;
}

static bool
ShouldIgnoreForRevertLayerValue(const nsString& aTargetValue,
                                SheetType aCandidateLevel,
                                bool aCandidateImportant,
                                int32_t aCandidateLayerIndex)
{
  SheetType sourceLevel;
  bool sourceImportant;
  int32_t sourceLayerIndex;
  if (!UnpackRevertLayerData(aTargetValue, sourceLevel, sourceImportant,
                             sourceLayerIndex)) {
    return false;
  }

  return ShouldIgnoreForRevertLayerData(sourceLevel, sourceImportant,
                                        sourceLayerIndex, aCandidateLevel,
                                        aCandidateImportant,
                                        aCandidateLayerIndex);
}

static bool
ShouldIgnoreForRevertLayerValue(const nsString& aTargetValue,
                                const nsRuleData* aRuleData)
{
  return ShouldIgnoreForRevertLayerValue(aTargetValue, aRuleData->mLevel,
                                         aRuleData->mIsImportantRule,
                                         aRuleData->mLayerIndex);
}

static bool
ShouldIgnoreForRevertLayerSourceValue(const nsString& aTargetValue,
                                      const nsString& aCandidateValue)
{
  SheetType candidateLevel;
  bool candidateImportant;
  int32_t candidateLayerIndex;
  if (!UnpackRevertLayerData(aCandidateValue, candidateLevel,
                             candidateImportant, candidateLayerIndex)) {
    return false;
  }

  return ShouldIgnoreForRevertLayerValue(aTargetValue, candidateLevel,
                                         candidateImportant,
                                         candidateLayerIndex);
}

static bool
IsComputedRevertLayerValue(const nsString& aValue)
{
  nsAutoString trimmed(aValue);
  trimmed.CompressWhitespace(true, true);
  return trimmed.EqualsLiteral("revert-layer");
}

struct CSSVariableDeclarations::Data : public RefCounted<Data>,
                                       public LinkedListElement<Data>
{
  struct Entry
  {
    uint32_t mNameOffset;
    uint32_t mNameLength;
    uint32_t mValueOffset;
    uint32_t mValueLength;
  };

  MOZ_DECLARE_REFCOUNTED_TYPENAME(CSSVariableDeclarations::Data)

  // The registry is weak and main-thread-only, just like the interning table.
  // Keep its sentinel alive through late stylesheet destruction at shutdown.
  static LinkedList<Data>& LiveData()
  {
    static auto* list = new LinkedList<Data>();
    return *list;
  }

  Data()
  {
    MOZ_ASSERT(NS_IsMainThread());
    LiveData().insertBack(this);
  }

  void AddRef() const
  {
    MOZ_ASSERT(NS_IsMainThread());
    RefCounted<Data>::AddRef();
  }

  void Release() const
  {
    MOZ_ASSERT(NS_IsMainThread());
    RefCounted<Data>::Release();
  }

  nsTArray<char16_t> mStrings;
  nsTArray<Entry> mEntries;
  // Open-addressed index of entry index + 1.  Empty for small sets, where a
  // linear scan is cheaper than the index allocation.
  nsTArray<uint32_t> mIndex;
  uint32_t mIndexMask = 0;
  uint32_t mLiveStringLength = 0;

  nsDataHashtable<nsStringHashKey, nsString> mRevertLayerSources;
  nsClassHashtable<nsStringHashKey, RevertLayerFallbackList>
    mRevertLayerFallbacks;

  uint64_t mHash = 0;
  bool mInterned = false;

  ~Data()
  {
    MOZ_ASSERT(NS_IsMainThread());
    if (mInterned) {
      RemoveInternedData(this);
    }
    remove();
  }

  const char16_t* NameData(const Entry& aEntry) const
  {
    return mStrings.Elements() + aEntry.mNameOffset;
  }

  const char16_t* ValueData(const Entry& aEntry) const
  {
    return mStrings.Elements() + aEntry.mValueOffset;
  }

  nsDependentString Name(const Entry& aEntry) const
  {
    return nsDependentString(NameData(aEntry), aEntry.mNameLength);
  }

  nsDependentString Value(const Entry& aEntry) const
  {
    return nsDependentString(ValueData(aEntry), aEntry.mValueLength);
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

  bool NameEquals(const Entry& aEntry, const nsAString& aName) const
  {
    return aEntry.mNameLength == aName.Length() &&
           !memcmp(NameData(aEntry), aName.BeginReading(),
                   aEntry.mNameLength * sizeof(char16_t));
  }

  void InsertIndex(size_t aIndex)
  {
    MOZ_ASSERT(!mIndex.IsEmpty());
    uint32_t slot = NameHash(Name(mEntries[aIndex])) & mIndexMask;
    while (mIndex[slot]) {
      slot = (slot + 1) & mIndexMask;
    }
    mIndex[slot] = static_cast<uint32_t>(aIndex + 1);
  }

  void BuildIndex(uint32_t aCapacity)
  {
    uint32_t capacity = 8;
    while (capacity < aCapacity) {
      capacity <<= 1;
    }
    mIndex.Clear();
    mIndex.SetLength(capacity);
    memset(mIndex.Elements(), 0, capacity * sizeof(uint32_t));
    mIndexMask = capacity - 1;
    for (size_t i = 0, n = mEntries.Length(); i < n; i++) {
      InsertIndex(i);
    }
  }

  size_t Find(const nsAString& aName) const
  {
    if (!mIndex.IsEmpty()) {
      uint32_t slot = NameHash(aName) & mIndexMask;
      while (uint32_t entry = mIndex[slot]) {
        size_t index = entry - 1;
        if (NameEquals(mEntries[index], aName)) {
          return index;
        }
        slot = (slot + 1) & mIndexMask;
      }
      return nsTArray<Entry>::NoIndex;
    }
    for (size_t i = 0, n = mEntries.Length(); i < n; i++) {
      if (NameEquals(mEntries[i], aName)) {
        return i;
      }
    }
    return nsTArray<Entry>::NoIndex;
  }

  bool Get(const nsAString& aName, nsString& aValue) const
  {
    size_t index = Find(aName);
    if (index == nsTArray<Entry>::NoIndex) {
      return false;
    }
    aValue.Assign(Value(mEntries[index]));
    return true;
  }

  void MaybeCompactStrings()
  {
    if (mStrings.Length() > mLiveStringLength * 2 + 64) {
      CompactStrings();
    }
  }

  void CompactStrings()
  {
    nsTArray<char16_t> compact;
    compact.SetCapacity(mLiveStringLength);
    for (size_t i = 0, n = mEntries.Length(); i < n; i++) {
      Entry& entry = mEntries[i];
      uint32_t nameOffset = static_cast<uint32_t>(compact.Length());
      compact.AppendElements(NameData(entry), entry.mNameLength);
      entry.mNameOffset = nameOffset;
      uint32_t valueOffset = static_cast<uint32_t>(compact.Length());
      compact.AppendElements(ValueData(entry), entry.mValueLength);
      entry.mValueOffset = valueOffset;
    }
    mStrings.SwapElements(compact);
  }

  void Put(const nsAString& aName, const nsAString& aValue)
  {
    size_t index = Find(aName);
    if (index != nsTArray<Entry>::NoIndex) {
      Entry& entry = mEntries[index];
      mLiveStringLength -= entry.mValueLength;
      entry.mValueOffset = AppendString(aValue);
      entry.mValueLength = aValue.Length();
      mLiveStringLength += entry.mValueLength;
      MaybeCompactStrings();
      return;
    }

    Entry entry;
    entry.mNameOffset = AppendString(aName);
    entry.mNameLength = aName.Length();
    entry.mValueOffset = AppendString(aValue);
    entry.mValueLength = aValue.Length();
    mLiveStringLength += entry.mNameLength + entry.mValueLength;
    mEntries.AppendElement(entry);

    if (!mIndex.IsEmpty()) {
      if ((mEntries.Length() + 1) * 2 > mIndex.Length()) {
        BuildIndex(mIndex.Length() * 2);
      } else {
        InsertIndex(mEntries.Length() - 1);
      }
    } else if (mEntries.Length() > 8) {
      BuildIndex(16);
    }
  }

  void Remove(const nsAString& aName)
  {
    size_t index = Find(aName);
    if (index == nsTArray<Entry>::NoIndex) {
      return;
    }
    mLiveStringLength -=
      mEntries[index].mNameLength + mEntries[index].mValueLength;
    mEntries.RemoveElementAt(index);
    if (!mIndex.IsEmpty()) {
      BuildIndex(mIndex.Length());
    }
    MaybeCompactStrings();
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
    Mix(hash, static_cast<uint64_t>(mEntries.Length()));
    for (size_t i = 0, n = mEntries.Length(); i < n; i++) {
      const Entry& entry = mEntries[i];
      Mix(hash, static_cast<uint64_t>(entry.mNameLength));
      Mix(hash, NameData(entry), entry.mNameLength);
      Mix(hash, static_cast<uint64_t>(entry.mValueLength));
      Mix(hash, ValueData(entry), entry.mValueLength);
    }
    return hash;
  }

  bool Equals(const Data& aOther) const
  {
    if (mEntries.Length() != aOther.mEntries.Length()) {
      return false;
    }
    for (size_t i = 0, n = mEntries.Length(); i < n; i++) {
      const Entry& left = mEntries[i];
      const Entry& right = aOther.mEntries[i];
      if (left.mNameLength != right.mNameLength ||
          left.mValueLength != right.mValueLength ||
          memcmp(NameData(left), aOther.NameData(right),
                 left.mNameLength * sizeof(char16_t)) ||
          memcmp(ValueData(left), aOther.ValueData(right),
                 left.mValueLength * sizeof(char16_t))) {
        return false;
      }
    }
    return true;
  }

  already_AddRefed<Data> Clone() const
  {
    RefPtr<Data> copy = new Data();
    copy->mStrings = mStrings;
    copy->mEntries = mEntries;
    copy->mIndex = mIndex;
    copy->mIndexMask = mIndexMask;
    copy->mLiveStringLength = mLiveStringLength;
    for (auto iter = mRevertLayerSources.ConstIter(); !iter.Done();
         iter.Next()) {
      copy->mRevertLayerSources.Put(iter.Key(), iter.UserData());
    }
    for (auto iter = mRevertLayerFallbacks.ConstIter(); !iter.Done();
         iter.Next()) {
      RevertLayerFallbackList* list =
        copy->mRevertLayerFallbacks.LookupOrAdd(iter.Key());
      list->mItems = iter.UserData()->mItems;
    }
    return copy.forget();
  }

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                            nsTHashtable<nsPtrHashKey<nsStringBuffer>>& aSeen) const
  {
    size_t n = aMallocSizeOf(this);
    n += mStrings.ShallowSizeOfExcludingThis(aMallocSizeOf);
    n += mEntries.ShallowSizeOfExcludingThis(aMallocSizeOf);
    n += mIndex.ShallowSizeOfExcludingThis(aMallocSizeOf);
    auto stringSize = [&](const nsAString& aString) -> size_t {
      nsStringBuffer* buffer = nsStringBuffer::FromString(aString);
      if (buffer) {
        if (aSeen.Contains(buffer)) {
          return 0;
        }
        aSeen.PutEntry(buffer);
      }
      return aString.SizeOfExcludingThisEvenIfShared(aMallocSizeOf);
    };
    n += mRevertLayerSources.ShallowSizeOfExcludingThis(aMallocSizeOf);
    for (auto iter = mRevertLayerSources.ConstIter(); !iter.Done();
         iter.Next()) {
      n += stringSize(iter.Key());
      n += stringSize(iter.Data());
    }
    n += mRevertLayerFallbacks.ShallowSizeOfExcludingThis(aMallocSizeOf);
    for (auto iter = mRevertLayerFallbacks.ConstIter(); !iter.Done();
         iter.Next()) {
      n += stringSize(iter.Key());
      const RevertLayerFallbackList* list = iter.UserData();
      n += aMallocSizeOf(list);
      n += list->mItems.ShallowSizeOfExcludingThis(aMallocSizeOf);
      for (uint32_t i = 0, len = list->mItems.Length(); i < len; i++) {
        n += stringSize(list->mItems[i].mValue);
        n += stringSize(list->mItems[i].mSource);
      }
    }
    return n;
  }
};

// Weak cache of immutable authored variable sets.  Entries are removed by the
// Data destructor, so the cache never owns data that no declaration uses.
// All accesses and Data reference-count operations are confined to the main
// thread.  A table mutex alone would not protect a weak pointer from destruction.
static nsDataHashtable<nsUint64HashKey, void*>* sAuthoredCache =
  new nsDataHashtable<nsUint64HashKey, void*>();

/* static */ void
CSSVariableDeclarations::RemoveInternedData(void* aData)
{
  MOZ_ASSERT(NS_IsMainThread());
  Data* data = static_cast<Data*>(aData);
  void* cached = nullptr;
  if (sAuthoredCache->Get(data->mHash, &cached) && cached == data) {
    sAuthoredCache->Remove(data->mHash);
  }
}

CSSVariableDeclarations::CSSVariableDeclarations()
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_COUNT_CTOR(CSSVariableDeclarations);
}

CSSVariableDeclarations::CSSVariableDeclarations(
    const CSSVariableDeclarations& aOther)
  : mData(aOther.mData)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_COUNT_CTOR(CSSVariableDeclarations);
}

CSSVariableDeclarations::~CSSVariableDeclarations()
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_COUNT_DTOR(CSSVariableDeclarations);
}

CSSVariableDeclarations&
CSSVariableDeclarations::operator=(const CSSVariableDeclarations& aOther)
{
  MOZ_ASSERT(NS_IsMainThread());
  if (this != &aOther) {
    mData = aOther.mData;
  }
  return *this;
}

CSSVariableDeclarations::Data*
CSSVariableDeclarations::EnsureWritableData()
{
  MOZ_ASSERT(NS_IsMainThread());
  if (!mData) {
    mData = new Data();
  } else if (mData->refCount() > 1 || mData->mInterned) {
    mData = mData->Clone();
  }
  return mData;
}

uint32_t
CSSVariableDeclarations::Count() const
{
  MOZ_ASSERT(NS_IsMainThread());
  return mData ? mData->mEntries.Length() : 0;
}

bool
CSSVariableDeclarations::IsInterned() const
{
  MOZ_ASSERT(NS_IsMainThread());
  return mData && mData->mInterned;
}

void
CSSVariableDeclarations::Intern()
{
  MOZ_ASSERT(NS_IsMainThread());
  if (!mData || mData->mInterned ||
      !Preferences::GetBool("layout.css.vars.authored.intern", true)) {
    return;
  }
  InternWithHash(mData->Hash());
}

void
CSSVariableDeclarations::InternWithHash(uint64_t aHash)
{
  MOZ_ASSERT(NS_IsMainThread());
  if (!mData || mData->mInterned) {
    return;
  }
  // The revert-layer side tables are rare and have their own lifetime rules;
  // leave those sets alone.
  if (!mData->mRevertLayerSources.IsEmpty() ||
      !mData->mRevertLayerFallbacks.IsEmpty()) {
    return;
  }

  void* cached = nullptr;
  if (sAuthoredCache->Get(aHash, &cached)) {
    if (static_cast<Data*>(cached)->Equals(*mData)) {
      mData = static_cast<Data*>(cached);
    }
    // Keep the original cache member on a collision.  The unequal block stays
    // uninterned (and is still in LiveData for unique memory accounting).
    return;
  }
  mData->mHash = aHash;
  sAuthoredCache->Put(aHash, mData);
  mData->mInterned = true;
}

/* static */ void
CSSVariableDeclarations::GetMemoryStats(mozilla::MallocSizeOf aMallocSizeOf,
                                        size_t* aBytes, size_t* aUniqueBlocks,
                                        size_t* aReferences)
{
  MOZ_ASSERT(NS_IsMainThread());
  size_t bytes = 0, blocks = 0, refs = 0;
  nsTHashtable<nsPtrHashKey<nsStringBuffer>> seen;
  for (Data* data = Data::LiveData().getFirst(); data; data = data->getNext()) {
    bytes += data->SizeOfIncludingThis(aMallocSizeOf, seen);
    blocks++;
    refs += data->refCount();
  }
  bytes += sAuthoredCache->ShallowSizeOfExcludingThis(aMallocSizeOf);
  bytes += aMallocSizeOf(sAuthoredCache);
  bytes += aMallocSizeOf(&Data::LiveData());
  *aBytes = bytes;
  *aUniqueBlocks = blocks;
  *aReferences = refs;
}

void
CSSVariableDeclarations::CopyVariablesFrom(
    const CSSVariableDeclarations& aOther)
{
  if (!aOther.mData) {
    return;
  }
  Data* data = EnsureWritableData();
  for (size_t i = 0, n = aOther.mData->mEntries.Length(); i < n; i++) {
    const Data::Entry& entry = aOther.mData->mEntries[i];
    data->Put(aOther.mData->Name(entry), nsDependentString(
      aOther.mData->Value(entry)));
  }
  for (auto iter = aOther.mData->mRevertLayerSources.ConstIter();
       !iter.Done(); iter.Next()) {
    data->mRevertLayerSources.Put(iter.Key(), iter.UserData());
  }
  for (auto iter = aOther.mData->mRevertLayerFallbacks.ConstIter();
       !iter.Done(); iter.Next()) {
    RevertLayerFallbackList* list =
      data->mRevertLayerFallbacks.LookupOrAdd(iter.Key());
    list->mItems = iter.UserData()->mItems;
  }
}

bool
CSSVariableDeclarations::Has(const nsAString& aName) const
{
  MOZ_ASSERT(NS_IsMainThread());
  return mData && mData->Find(aName) != nsTArray<Data::Entry>::NoIndex;
}

bool
CSSVariableDeclarations::Get(const nsAString& aName,
                             Type& aType,
                             nsString& aTokenStream) const
{
  MOZ_ASSERT(NS_IsMainThread());
  if (!mData) {
    return false;
  }
  size_t index = mData->Find(aName);
  if (index == nsTArray<Data::Entry>::NoIndex) {
    return false;
  }
  nsDependentString value = mData->Value(mData->mEntries[index]);
  if (value.EqualsLiteral(INITIAL_VALUE)) {
    aType = eInitial;
    aTokenStream.Truncate();
  } else if (value.EqualsLiteral(INHERIT_VALUE)) {
    aType = eInitial;
    aTokenStream.Truncate();
  } else if (value.EqualsLiteral(UNSET_VALUE)) {
    aType = eUnset;
    aTokenStream.Truncate();
  } else if (value.EqualsLiteral(REVERT_VALUE)) {
    aType = eRevert;
    aTokenStream.Truncate();
  } else if (IsRevertLayerValue(value)) {
    aType = eRevertLayer;
    aTokenStream.Truncate();
  } else {
    aType = eTokenStream;
    aTokenStream = value;
  }
  return true;
}

void
CSSVariableDeclarations::PutTokenStream(const nsAString& aName,
                                        const nsString& aTokenStream)
{
  MOZ_ASSERT(!aTokenStream.EqualsLiteral(INITIAL_VALUE) &&
             !aTokenStream.EqualsLiteral(INHERIT_VALUE) &&
             !aTokenStream.EqualsLiteral(UNSET_VALUE) &&
             !aTokenStream.EqualsLiteral(REVERT_VALUE) &&
             !aTokenStream.EqualsLiteral(REVERT_LAYER_VALUE));
  EnsureWritableData()->Put(aName, aTokenStream);
}

void
CSSVariableDeclarations::PutInitial(const nsAString& aName)
{
  EnsureWritableData()->Put(aName, NS_LITERAL_STRING(INITIAL_VALUE));
}

void
CSSVariableDeclarations::PutInherit(const nsAString& aName)
{
  EnsureWritableData()->Put(aName, NS_LITERAL_STRING(INHERIT_VALUE));
}

void
CSSVariableDeclarations::PutUnset(const nsAString& aName)
{
  EnsureWritableData()->Put(aName, NS_LITERAL_STRING(UNSET_VALUE));
}

void
CSSVariableDeclarations::PutRevert(const nsAString& aName)
{
  EnsureWritableData()->Put(aName, NS_LITERAL_STRING(REVERT_VALUE));
}

void
CSSVariableDeclarations::PutRevertLayer(const nsAString& aName)
{
  EnsureWritableData()->Put(aName, NS_LITERAL_STRING(REVERT_LAYER_VALUE));
}

void
CSSVariableDeclarations::Remove(const nsAString& aName)
{
  MOZ_ASSERT(NS_IsMainThread());
  if (mData) {
    EnsureWritableData()->Remove(aName);
  }
}

void
CSSVariableDeclarations::MapRuleInfoInto(nsRuleData* aRuleData)
{
  MOZ_ASSERT(NS_IsMainThread());
  if (!(aRuleData->mSIDs & NS_STYLE_INHERIT_BIT(Variables)) || !mData) {
    return;
  }

  if (!aRuleData->mVariables) {
    aRuleData->mVariables = new CSSVariableDeclarations;
  }

  Data* target = aRuleData->mVariables->EnsureWritableData();
  for (size_t index = 0, n = mData->mEntries.Length(); index < n; index++) {
    const Data::Entry& entry = mData->mEntries[index];
    nsDependentString name = mData->Name(entry);
    nsString value = mData->Value(entry);
    nsString valueSource;
    SetRevertLayerData(valueSource, aRuleData);
    if (value.EqualsLiteral(REVERT_LAYER_VALUE)) {
      value = valueSource;
      aRuleData->mConditions.SetUncacheable();
    }

    nsString existingValue;
    if (target->Get(name, existingValue)) {
      if (IsRevertLayerValue(existingValue)) {
        if (ShouldIgnoreForRevertLayerValue(existingValue, aRuleData)) {
          continue;
        }
      } else {
        nsString sourceValue;
        if (target->mRevertLayerSources.Get(name, &sourceValue) &&
            !ShouldIgnoreForRevertLayerValue(sourceValue, aRuleData)) {
          RevertLayerFallbackList* fallbacks =
            target->mRevertLayerFallbacks.LookupOrAdd(name);
          RevertLayerFallback* fallback = fallbacks->mItems.AppendElement();
          fallback->mValue = value;
          fallback->mSource = valueSource;
        }
        continue;
      }
    }

    target->Put(name, value);
    if (ValueMightResolveToRevertLayer(value)) {
      target->mRevertLayerSources.Put(name, valueSource);
    } else {
      target->mRevertLayerSources.Remove(name);
      target->mRevertLayerFallbacks.Remove(name);
    }
  }
}

void
CSSVariableDeclarations::AddVariablesToResolver(
                                           CSSVariableResolver* aResolver) const
{
  MOZ_ASSERT(NS_IsMainThread());
  if (!mData) {
    return;
  }
  // Computed custom-property enumeration order is unspecified by CSSOM.
  // Iterate the compact records directly; cascade winners have already been
  // selected by MapRuleInfoInto, independently of their enumeration order.
  for (size_t index = 0, n = mData->mEntries.Length(); index < n; index++) {
    const Data::Entry& entry = mData->mEntries[index];
    nsDependentString name = mData->Name(entry);
    nsDependentString value = mData->Value(entry);
    if (value.EqualsLiteral(INITIAL_VALUE)) {
      aResolver->Put(name, EmptyString(),
                     eCSSTokenSerialization_Nothing,
                     eCSSTokenSerialization_Nothing,
                     false);
    } else if (value.EqualsLiteral(INHERIT_VALUE) ||
               value.EqualsLiteral(UNSET_VALUE) ||
               value.EqualsLiteral(REVERT_VALUE) ||
               IsRevertLayerValue(value)) {
      // Keep the inherited value.
    } else {
      aResolver->Put(name, value,
                     eCSSTokenSerialization_Nothing,
                     eCSSTokenSerialization_Nothing,
                     false);
    }
  }
}

void
CSSVariableDeclarations::ResolveRevertLayerFallbacks(
                                             const CSSVariableValues* aInherited,
                                             CSSVariableValues* aOutput) const
{
  MOZ_ASSERT(NS_IsMainThread());
  if (!mData) {
    return;
  }
  for (auto iter = mData->mRevertLayerSources.ConstIter();
       !iter.Done(); iter.Next()) {
    const nsAString& name = iter.Key();
    nsString value;
    nsCSSTokenSerializationType firstToken, lastToken;
    if (!aOutput->Get(name, value, firstToken, lastToken)) {
      continue;
    }

    if (!IsComputedRevertLayerValue(value)) {
      continue;
    }

    const RevertLayerFallbackList* fallbacks =
      mData->mRevertLayerFallbacks.Get(name);
    if (fallbacks) {
      nsString sourceValue = iter.UserData();
      bool foundFallback = false;
      for (uint32_t i = 0, n = fallbacks->mItems.Length(); i < n; i++) {
        const RevertLayerFallback& fallback = fallbacks->mItems[i];
        if (ShouldIgnoreForRevertLayerSourceValue(sourceValue,
                                                 fallback.mSource)) {
          continue;
        }

        if (IsRevertLayerValue(fallback.mValue)) {
          sourceValue = fallback.mSource;
          continue;
        }

        CSSVariableValues inheritedValues(*aOutput);
        if (aInherited->Get(name, value, firstToken, lastToken)) {
          inheritedValues.Put(name, value, firstToken, lastToken);
        } else {
          inheritedValues.Remove(name);
        }

        CSSVariableDeclarations fallbackDeclarations;
        fallbackDeclarations.EnsureWritableData()->Put(name, fallback.mValue);

        CSSVariableValues fallbackValues;
        CSSVariableResolver resolver(&fallbackValues);
        resolver.Resolve(&inheritedValues, &fallbackDeclarations);

        if (!fallbackValues.Get(name, value, firstToken, lastToken)) {
          aOutput->Remove(name);
          foundFallback = true;
          break;
        }

        if (IsComputedRevertLayerValue(value)) {
          sourceValue = fallback.mSource;
          continue;
        }

        aOutput->Put(name, value, firstToken, lastToken);
        foundFallback = true;
        break;
      }

      if (foundFallback) {
        continue;
      }
    }

    if (aInherited->Get(name, value, firstToken, lastToken)) {
      aOutput->Put(name, value, firstToken, lastToken);
    } else {
      aOutput->Remove(name);
    }
  }
}

size_t
CSSVariableDeclarations::SizeOfIncludingThis(
                                      mozilla::MallocSizeOf aMallocSizeOf) const
{
  MOZ_ASSERT(NS_IsMainThread());
  return aMallocSizeOf(this);
}
} // namespace mozilla
