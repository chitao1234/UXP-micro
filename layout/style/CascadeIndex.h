/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef CascadeIndex_h
#define CascadeIndex_h

#include <algorithm>

#include "mozilla/HashFunctions.h"
#include "mozilla/RefCounted.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Preferences.h"
#include "nsDataHashtable.h"
#include "nsHashKeys.h"
#include "nsTHashtable.h"
#include "nsThreadUtils.h"

class RuleHash;

// One identity domain per CSS rule processor generation. IDs are NOT cascade
// ranks: every media state keeps its own rank vector. Distinguish repeated
// occurrences of a selector (e.g. the same imported sheet used twice).
class CascadeIndexPool final : public mozilla::RefCounted<CascadeIndexPool>
{
public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(CascadeIndexPool)
  uint32_t RuleId(const void* aRule, const void* aSelector, uint32_t aOccurrence);
  void AddOwner(const RuleHash* aOwner) { mOwners.AppendElement(aOwner); }
  void RemoveOwner(const RuleHash* aOwner) { mOwners.RemoveElement(aOwner); }
  size_t SizeOfIncludingThis(const RuleHash* aOwner,
                            mozilla::MallocSizeOf aMallocSizeOf) const;
  ~CascadeIndexPool() { MOZ_ASSERT(mOwners.IsEmpty()); }

private:
  struct Key {
    const void* mRule;
    const void* mSelector;
    uint32_t mOccurrence;
  };
  class Entry : public PLDHashEntryHdr {
  public:
    typedef const Key& KeyType;
    typedef const Key* KeyTypePointer;
    explicit Entry(KeyTypePointer aKey) : mKey(*aKey), mId(0) {}
    bool KeyEquals(KeyTypePointer aKey) const {
      return mKey.mRule == aKey->mRule && mKey.mSelector == aKey->mSelector &&
             mKey.mOccurrence == aKey->mOccurrence;
    }
    static KeyTypePointer KeyToPointer(KeyType aKey) { return &aKey; }
    static PLDHashNumber HashKey(KeyTypePointer aKey) {
      return mozilla::HashGeneric(aKey->mRule, aKey->mSelector, aKey->mOccurrence);
    }
    enum { ALLOW_MEMMOVE = true };
    Key mKey;
    uint32_t mId;
  };
  nsTHashtable<Entry> mIds;
  nsTArray<const RuleHash*> mOwners;
};

template<class T> uint32_t CascadeIndexHash(T* aValue) {
  return mozilla::HashGeneric(aValue);
}

// Build locally, then canonicalize complete immutable buckets. The cache is
// weak and domain-specific; blocks keep their domain alive, never a document or
// stylesheet. Each block tracks its list owners so reports count it once even
// when it is referenced from several inactive media states.
template<class T, size_t Inline = 0>
class CascadeIndexList {
  struct Block;
  typedef nsDataHashtable<nsUint32HashKey, Block*> Cache;
  struct Block final : public mozilla::RefCounted<Block> {
    MOZ_DECLARE_REFCOUNTED_TYPENAME(CascadeIndexList::Block)
    AutoTArray<T, Inline> mValues;
    RefPtr<CascadeIndexPool> mPool;
    Block* mNext = nullptr;
    CascadeIndexList* mOwners = nullptr;
    uint32_t mHash = 0;
    static Cache*& Table() { static Cache* table = nullptr; return table; }
    ~Block() {
      MOZ_ASSERT(NS_IsMainThread());
      MOZ_ASSERT(!mOwners);
      if (!mPool) return;
      Cache*& table = Table();
      Block* head = table->Get(mHash);
      if (head == this) {
        if (mNext) table->Put(mHash, mNext);
        else table->Remove(mHash);
      } else {
        while (head && head->mNext != this) head = head->mNext;
        MOZ_RELEASE_ASSERT(head, "Missing weak cascade bucket");
        head->mNext = mNext;
      }
      if (table->Count() == 0) { delete table; table = nullptr; }
    }
  };
public:
  CascadeIndexList() = default;
  ~CascadeIndexList() { Detach(); }
  CascadeIndexList(const CascadeIndexList&) = delete;
  CascadeIndexList& operator=(const CascadeIndexList&) = delete;
  size_t Length() const { return mBlock ? mBlock->mValues.Length() : 0; }
  bool IsEmpty() const { return !Length(); }
  const T* Elements() const {
    if (mBlock) return mBlock->mValues.Elements();
    static const nsTArray<T> empty;
    return empty.Elements();
  }
  const T& operator[](size_t aIndex) const { return mBlock->mValues[aIndex]; }
  void AppendElement(const T& aValue) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_RELEASE_ASSERT(!mBlock || !mBlock->mPool, "Mutating frozen cascade index");
    if (!mBlock) Attach(new Block);
    mBlock->mValues.AppendElement(aValue);
  }
  void SwapElements(CascadeIndexList& aOther) {
    RefPtr<Block> left = mBlock, right = aOther.mBlock;
    Detach(); aOther.Detach();
    if (right) Attach(right);
    if (left) aOther.Attach(left);
  }
  template<class F> void TransformValues(F aTransform) {
    MOZ_RELEASE_ASSERT(!mBlock || !mBlock->mPool, "Editing frozen cascade bucket");
    if (mBlock) for (T& value : mBlock->mValues) aTransform(value);
  }
  void Freeze(CascadeIndexPool* aPool) {
    if (!aPool || !mBlock || mBlock->mPool) return;
    MOZ_ASSERT(NS_IsMainThread());
    uint32_t hash = mozilla::HashGeneric(aPool, uint32_t(Length()));
    for (const T& value : mBlock->mValues)
      hash = mozilla::AddToHash(hash, CascadeIndexHash(value));
    uint32_t bits = std::min(32u, mozilla::Preferences::GetUint(
      "layout.css.cascade-index.hash-bits", 32));
    if (bits < 32) hash &= (uint32_t(1) << bits) - 1;
    Cache*& table = Block::Table();
    if (!table) table = new Cache;
    for (Block* other = table->Get(hash); other; other = other->mNext) {
      if (other->mPool != aPool || other->mValues.Length() != Length()) continue;
      size_t i = 0;
      while (i < Length() && other->mValues[i] == mBlock->mValues[i]) ++i;
      if (i == Length()) { Detach(); Attach(other); return; }
    }
    mBlock->mValues.Compact();
    mBlock->mHash = hash;
    mBlock->mPool = aPool;
    mBlock->mNext = table->Get(hash);
    table->Put(hash, mBlock);
  }
  size_t ShallowSizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) const {
    if (!mBlock || mBlock->mOwners != this) return 0;
    size_t n = aMallocSizeOf(mBlock) +
      mBlock->mValues.ShallowSizeOfExcludingThis(aMallocSizeOf);
    if (mBlock->mPool) {
      Cache* table = Block::Table();
      auto iter = table->ConstIter();
      if (iter.Data() == mBlock) {
        n += aMallocSizeOf(table) + table->ShallowSizeOfExcludingThis(aMallocSizeOf);
      }
    }
    return n;
  }
private:
  void Attach(Block* aBlock) {
    mBlock = aBlock;
    mNextOwner = aBlock->mOwners;
    aBlock->mOwners = this;
  }
  void Detach() {
    if (!mBlock) return;
    CascadeIndexList** owner = &mBlock->mOwners;
    while (*owner != this) {
      MOZ_RELEASE_ASSERT(*owner, "Missing cascade list owner");
      owner = &(*owner)->mNextOwner;
    }
    *owner = mNextOwner;
    mNextOwner = nullptr;
    mBlock = nullptr;
  }
  RefPtr<Block> mBlock;
  CascadeIndexList* mNextOwner = nullptr;
};
#endif
