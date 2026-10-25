/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* computed CSS Variable values */

#ifndef mozilla_CSSVariableValues_h
#define mozilla_CSSVariableValues_h

#include "mozilla/MemoryReporting.h"
#include "mozilla/RefPtr.h"
#include "nsCSSScanner.h"
#include "nsDataHashtable.h"
#include "nsTArray.h"

namespace mozilla {

class CSSVariableResolver;

class CSSVariableValues
{
public:
  CSSVariableValues();
  CSSVariableValues(const CSSVariableValues& aOther);
  ~CSSVariableValues();
  CSSVariableValues& operator=(const CSSVariableValues& aOther);

  /**
   * Returns the heap memory currently held by all live CSSVariableValues
   * objects.  The objects themselves are usually arena-allocated and are not
   * included.
   */
  static size_t SizeOfLive(mozilla::MallocSizeOf aMallocSizeOf);

  bool operator==(const CSSVariableValues& aOther) const;
  bool operator!=(const CSSVariableValues& aOther) const
    { return !(*this == aOther); }

  /**
   * Returns whether the two sets have the same names and values in the same
   * order (including the token types).  This is used to share an unchanged
   * computed set with its parent, so it is intentionally stricter than
   * operator==.
   */
  bool EqualsInOrder(const CSSVariableValues& aOther) const;

  /**
   * Gets the value of a variable in this set of computed variables.
   *
   * @param aName The variable name (not including any "--" prefix that would
   *   be part of the custom property name).
   * @param aValue Out parameter into which the value of the variable will
   *   be stored.
   * @return Whether a variable with the given name was found.  When false
   *   is returned, aValue will not be modified.
   */
  bool Get(const nsAString& aName, nsString& aValue) const;

  /**
   * Gets the value of a variable in this set of computed variables, along
   * with information on the types of tokens that appear at the start and
   * end of the token stream.
   *
   * @param aName The variable name (not including any "--" prefix that would
   *   be part of the custom property name).
   * @param aValue Out parameter into which the value of the variable will
   *   be stored.
   * @param aFirstToken The type of token at the start of the variable value.
   * @param aLastToken The type of token at the en of the variable value.
   * @return Whether a variable with the given name was found.  When false
   *   is returned, aValue, aFirstToken and aLastToken will not be modified.
   */
  bool Get(const nsAString& aName,
           nsString& aValue,
           nsCSSTokenSerializationType& aFirstToken,
           nsCSSTokenSerializationType& aLastToken) const;

  /**
   * Gets the name of the variable at the given index.
   *
   * Variables on this object are ordered, and that order is just determined
   * based on the order that they are added to the object.  A consistent
   * ordering is required for CSSDeclaration objects in the DOM.
   * CSSDeclarations expose property names as indexed properties, which need to
   * be stable.
   *
   * @param aIndex The index of the variable to get.
   * @param aName Out parameter into which the name of the variable will be
   *   stored.
   */
  void GetVariableAt(size_t aIndex, nsAString& aName) const;

  /**
   * Gets the number of variables stored on this object.
   */
  size_t Count() const;

  /**
   * Adds or modifies an existing entry in this set of variable values.
   *
   * @param aName The variable name (not including any "--" prefix that would
   *   be part of the custom property name) whose value is to be set.
   * @param aValue The variable value.
   * @param aFirstToken The type of token at the start of the variable value.
   * @param aLastToken The type of token at the en of the variable value.
   */
  void Put(const nsAString& aName,
           nsString aValue,
           nsCSSTokenSerializationType aFirstToken,
           nsCSSTokenSerializationType aLastToken);

  /**
   * Removes an entry in this set of variable values.
   */
  void Remove(const nsAString& aName);

  /**
   * Replaces this object's data with an equivalent interned block when one
   * already exists.  The data block is immutable once interned; mutations
   * detach through copy-on-write.
   */
  void Intern();

  /**
   * Copies the variables from this object into aResolver, marking them as
   * computed, inherited values.
   */
  void AddVariablesToResolver(CSSVariableResolver* aResolver) const;

private:
  struct Data;

  /**
   * Returns a writable data block, cloning the shared block if necessary.
   */
  Data* EnsureWritableData();

  RefPtr<Data> mData;

};

} // namespace mozilla

#endif
