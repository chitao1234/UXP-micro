/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* CSS Custom Property assignments for a Declaration at a given priority */

#ifndef mozilla_CSSVariableDeclarations_h
#define mozilla_CSSVariableDeclarations_h

#include "mozilla/MemoryReporting.h"
#include "mozilla/RefPtr.h"
#include "nsClassHashtable.h"
#include "nsDataHashtable.h"
#include "nsTArray.h"

namespace mozilla {
class CSSVariableResolver;
class CSSVariableValues;
} // namespace mozilla
struct nsRuleData;

namespace mozilla {

class CSSVariableDeclarations
{
public:
  // This Gecko style-engine representation is confined to the main thread,
  // including copies, reads, memory reporting and destruction.  Data uses
  // non-atomic reference counting; its weak cache does not extend its lifetime.
  CSSVariableDeclarations();
  CSSVariableDeclarations(const CSSVariableDeclarations& aOther);
  ~CSSVariableDeclarations();
  CSSVariableDeclarations& operator=(const CSSVariableDeclarations& aOther);

  /**
   * Returns whether this set of variable declarations includes a variable
   * with a given name.
   *
   * @param aName The variable name (not including any "--" prefix that would
   *   be part of the custom property name).
   */
  bool Has(const nsAString& aName) const;

  /**
   * Represents the type of a variable value.
   */
  enum Type {
    eTokenStream,  // a stream of CSS tokens (the usual type for variables)
    eInitial,      // 'initial'
    eInherit,      // 'inherit'
    eUnset,        // 'unset'
    eRevert,       // 'revert'
    eRevertLayer   // 'revert-layer'
  };

  /**
   * Gets the value of a variable in this set of variable declarations.
   *
   * @param aName The variable name (not including any "--" prefix that would
   *   be part of the custom property name).
   * @param aType Out parameter into which the type of the variable value will
   *   be stored.
   * @param aValue Out parameter into which the value of the variable will
   *   be stored.  If the variable is 'initial', 'inherit', 'unset', or
   *   'revert', this will be the empty string.
   * @return Whether a variable with the given name was found.  When false
   *   is returned, aType and aValue will not be modified.
   */
  bool Get(const nsAString& aName, Type& aType, nsString& aValue) const;

  /**
   * Adds or modifies an existing entry in this set of variable declarations
   * to have the value 'initial'.
   *
   * @param aName The variable name (not including any "--" prefix that would
   *   be part of the custom property name) whose value is to be set.
   */
  void PutInitial(const nsAString& aName);

  /**
   * Adds or modifies an existing entry in this set of variable declarations
   * to have the value 'inherit'.
   *
   * @param aName The variable name (not including any "--" prefix that would
   *   be part of the custom property name) whose value is to be set.
   */
  void PutInherit(const nsAString& aName);

  /**
   * Adds or modifies an existing entry in this set of variable declarations
   * to have the value 'unset'.
   *
   * @param aName The variable name (not including any "--" prefix that would
   *   be part of the custom property name) whose value is to be set.
   */
  void PutUnset(const nsAString& aName);

  /**
   * Adds or modifies an existing entry in this set of variable declarations
   * to have the value 'revert'.
   *
   * @param aName The variable name (not including any "--" prefix that would
   *   be part of the custom property name) whose value is to be set.
   */
  void PutRevert(const nsAString& aName);

  /**
   * Adds or modifies an existing entry in this set of variable declarations
   * to have the value 'revert-layer'.
   *
   * @param aName The variable name (not including any "--" prefix that would
   *   be part of the custom property name) whose value is to be set.
   */
  void PutRevertLayer(const nsAString& aName);

  /**
   * Adds or modifies an existing entry in this set of variable declarations
   * to have a token stream value.
   *
   * @param aName The variable name (not including any "--" prefix that would
   *   be part of the custom property name) whose value is to be set.
   * @param aTokenStream The CSS token stream.
   */
  void PutTokenStream(const nsAString& aName, const nsString& aTokenStream);

  /**
   * Removes an entry in this set of variable declarations.
   *
   * @param aName The variable name (not including any "--" prefix that would
   *   be part of the custom property name) whose entry is to be removed.
   */
  void Remove(const nsAString& aName);

  /**
   * Returns the number of entries in this set of variable declarations.
   */
  uint32_t Count() const;

  /**
   * Replaces the mutable representation with an equal shared immutable one
   * when possible.  Called when a declaration enters the rule tree.
   */
  void Intern();

  bool IsInterned() const;

  /**
   * Count every live Data allocation once, including uninterned copies and
   * transient cascade accumulators, plus the weak sharing table.  Wrappers
   * are counted by SizeOfIncludingThis in their owning declarations.
   */
  static void GetMemoryStats(mozilla::MallocSizeOf aMallocSizeOf,
                            size_t* aBytes, size_t* aUniqueBlocks,
                            size_t* aReferences);

  /**
   * Copies each variable value from this object into aRuleData, unless that
   * variable already exists on aRuleData.
   */
  void MapRuleInfoInto(nsRuleData* aRuleData);

  /**
   * Copies the variables from this object into aResolver, marking them as
   * specified values.
   */
  void AddVariablesToResolver(CSSVariableResolver* aResolver) const;

  /**
   * Applies stored lower-priority declarations for custom property values that
   * resolved to 'revert-layer'.
   */
  void ResolveRevertLayerFallbacks(const CSSVariableValues* aInherited,
                                   CSSVariableValues* aOutput) const;

  // Data is reported globally by GetMemoryStats, since even an uninterned
  // block can be shared by multiple wrappers through copy-on-write.
  size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;

private:
  friend class CSSVariableDeclarationsTest;
  struct RevertLayerFallback
  {
    nsString mValue;
    nsString mSource;
  };

  struct RevertLayerFallbackList
  {
    nsTArray<RevertLayerFallback> mItems;
  };

  /**
   * Adds all the variable declarations from aOther into this object.
   */
  void CopyVariablesFrom(const CSSVariableDeclarations& aOther);

  struct Data;

  Data* EnsureWritableData();
  const Data* GetData() const { return mData; }
  void EnsureHashOrder();
  void InternWithHash(uint64_t aHash);
  static void RemoveInternedData(void* aData);

  RefPtr<Data> mData;
};

} // namespace mozilla

#endif
