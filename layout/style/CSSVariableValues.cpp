/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* computed CSS Variable values */

#include "CSSVariableValues.h"

#include "CSSVariableResolver.h"

#include "mozilla/Mutex.h"

namespace mozilla {

// A live-value registry lets the memory reporter account for the heap data
// owned by every CSSVariableValues object without walking style contexts.
// The registry and its lock are intentionally process-lifetime objects: style
// values can be destroyed during shutdown after static destructors have run.
static Mutex* sLiveValuesMutex = new Mutex("CSSVariableValues");
static nsTArray<CSSVariableValues*>* sLiveValues =
  new nsTArray<CSSVariableValues*>();

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

CSSVariableValues::CSSVariableValues()
{
  MOZ_COUNT_CTOR(CSSVariableValues);
  RegisterVariableValues(this);
}

CSSVariableValues::CSSVariableValues(const CSSVariableValues& aOther)
{
  MOZ_COUNT_CTOR(CSSVariableValues);
  CopyVariablesFrom(aOther);
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
  if (this == &aOther) {
    return *this;
  }

  mVariableIDs.Clear();
  mVariables.Clear();
  CopyVariablesFrom(aOther);
  return *this;
}

bool
CSSVariableValues::operator==(const CSSVariableValues& aOther) const
{
  if (mVariables.Length() != aOther.mVariables.Length()) {
    return false;
  }

  for (size_t thisIndex = 0; thisIndex < mVariables.Length(); ++thisIndex) {
    size_t otherIndex;
    if (!aOther.mVariableIDs.Get(mVariables[thisIndex].mVariableName,
                                 &otherIndex)) {
      return false;
    }
    const nsString& otherValue = aOther.mVariables[otherIndex].mValue;
    if (!mVariables[thisIndex].mValue.Equals(otherValue)) {
      return false;
    }
  }

  return true;
}

size_t
CSSVariableValues::Count() const
{
  return mVariables.Length();
}

bool
CSSVariableValues::Get(const nsAString& aName, nsString& aValue) const
{
  size_t id;
  if (!mVariableIDs.Get(aName, &id)) {
    return false;
  }
  aValue = mVariables[id].mValue;
  return true;
}

bool
CSSVariableValues::Get(const nsAString& aName,
                       nsString& aValue,
                       nsCSSTokenSerializationType& aFirstToken,
                       nsCSSTokenSerializationType& aLastToken) const
{
  size_t id;
  if (!mVariableIDs.Get(aName, &id)) {
    return false;
  }
  aValue = mVariables[id].mValue;
  aFirstToken = mVariables[id].mFirstToken;
  aLastToken = mVariables[id].mLastToken;
  return true;
}

void
CSSVariableValues::GetVariableAt(size_t aIndex, nsAString& aName) const
{
  aName = mVariables[aIndex].mVariableName;
}

void
CSSVariableValues::Put(const nsAString& aName,
                       nsString aValue,
                       nsCSSTokenSerializationType aFirstToken,
                       nsCSSTokenSerializationType aLastToken)
{
  size_t id;
  if (mVariableIDs.Get(aName, &id)) {
    mVariables[id].mValue = aValue;
    mVariables[id].mFirstToken = aFirstToken;
    mVariables[id].mLastToken = aLastToken;
  } else {
    id = mVariables.Length();
    mVariableIDs.Put(aName, id);
    mVariables.AppendElement(Variable(aName, aValue, aFirstToken, aLastToken));
  }
}

void
CSSVariableValues::Remove(const nsAString& aName)
{
  size_t id;
  if (!mVariableIDs.Get(aName, &id)) {
    return;
  }

  mVariables.RemoveElementAt(id);
  mVariableIDs.Clear();
  for (size_t i = 0, n = mVariables.Length(); i < n; i++) {
    mVariableIDs.Put(mVariables[i].mVariableName, i);
  }
}

void
CSSVariableValues::CopyVariablesFrom(const CSSVariableValues& aOther)
{
  for (size_t i = 0, n = aOther.mVariables.Length(); i < n; i++) {
    Put(aOther.mVariables[i].mVariableName,
        aOther.mVariables[i].mValue,
        aOther.mVariables[i].mFirstToken,
        aOther.mVariables[i].mLastToken);
  }
}

void
CSSVariableValues::AddVariablesToResolver(CSSVariableResolver* aResolver) const
{
  for (size_t i = 0, n = mVariables.Length(); i < n; i++) {
    aResolver->Put(mVariables[i].mVariableName,
                   mVariables[i].mValue,
                   mVariables[i].mFirstToken,
                   mVariables[i].mLastToken,
                   true);
  }
}

size_t
CSSVariableValues::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const
{
  size_t n = mVariables.ShallowSizeOfExcludingThis(aMallocSizeOf);
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

/* static */ size_t
CSSVariableValues::SizeOfLive(MallocSizeOf aMallocSizeOf)
{
  size_t n = 0;
  MutexAutoLock lock(*sLiveValuesMutex);
  for (size_t i = 0, len = sLiveValues->Length(); i < len; i++) {
    n += (*sLiveValues)[i]->SizeOfExcludingThis(aMallocSizeOf);
  }
  return n;
}

} // namespace mozilla
