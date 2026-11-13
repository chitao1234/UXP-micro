/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef mozilla_dom_ImportMap_h
#define mozilla_dom_ImportMap_h

#include "mozilla/MemoryReporting.h"
#include "mozilla/UniquePtr.h"
#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsTArray.h"
#include "js/RootingAPI.h"

class nsIDocument;
class nsIURI;

namespace mozilla {
namespace dom {

// Document-owned, normalized import maps. No JS objects, document pointers or
// roots survive parsing. Descending sorted arrays implement spec code-unit
// ordering without one heap allocation per map entry.
class ImportMap final
{
public:
  static UniquePtr<ImportMap> Parse(JSContext* aCx, const nsAString& aText,
                                    nsIURI* aBase, nsIDocument* aDocument);
  already_AddRefed<nsIURI> Resolve(const nsAString& aSpecifier, nsIURI* aBase,
                                    bool aRecord = true);
  void Merge(ImportMap& aNewMap, nsIDocument* aDocument);
  bool GetIntegrity(nsIURI* aURI, nsAString& aResult) const;
  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const;

private:
  struct Entry {
    nsString key;
    nsCOMPtr<nsIURI> address; // null blocks resolution; it is not a miss.
  };
  using SpecifierMap = nsTArray<Entry>;
  struct Scope {
    nsCString key;
    SpecifierMap imports;
  };
  struct Integrity {
    nsCString url;
    nsString metadata;
  };
  struct Resolution {
    nsCString base;
    nsString specifier;
    bool prefixMatchAllowed;
  };
  SpecifierMap mImports;
  nsTArray<Scope> mScopes;
  nsTArray<Integrity> mIntegrity;
  nsTArray<Resolution> mResolved;

  static bool ParseSpecifiers(JSContext* aCx, JS::HandleObject aObject,
                              nsIURI* aBase, nsIDocument* aDocument,
                              SpecifierMap& aResult);
  static nsresult Match(const SpecifierMap& aMap, const nsAString& aKey,
                         bool aPrefixAllowed, nsIURI** aResult);
  void MergeSpecifiers(SpecifierMap& aOld, SpecifierMap& aNew,
                        const nsACString* aScope, nsIDocument* aDocument);
};

} // namespace dom
} // namespace mozilla
#endif
