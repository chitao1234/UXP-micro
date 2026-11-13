/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "ImportMap.h"
#include "jsapi.h"
#include "nsContentUtils.h"
#include "nsIScriptError.h"
#include "nsJSUtils.h"
#include "nsNetUtil.h"
#include <algorithm>

namespace mozilla {
namespace dom {

// Adapted from Firefox's js/loader/ImportMap.cpp and the HTML import-map
// processing model. Use the same URI parser as the existing module loader.
static void Warn(nsIDocument* aDocument, const char* aMessage)
{
  nsContentUtils::ReportToConsoleNonLocalized(NS_ConvertUTF8toUTF16(aMessage),
    nsIScriptError::warningFlag, NS_LITERAL_CSTRING("Import map"), aDocument);
}

static bool TypeError(JSContext* aCx, const char* aMessage)
{
  JS::RootedString message(aCx, JS_NewStringCopyZ(aCx, aMessage));
  JS::RootedString filename(aCx, JS_GetEmptyString(aCx));
  if (!message) return false;
  JS::RootedValue error(aCx);
  if (!JS::CreateError(aCx, JSEXN_TYPEERR, nullptr, filename, 0, 0, nullptr,
                        message, &error)) return false;
  JS_SetPendingException(aCx, error);
  return false;
}

static bool IsMap(JSContext* aCx, JS::HandleValue aValue)
{
  if (!aValue.isObject()) return false;
  bool array;
  return JS_IsArrayObject(aCx, aValue, &array) && !array;
}

static bool OwnProperty(JSContext* aCx, JS::HandleObject aObject,
                         const char* aName, JS::MutableHandleValue aValue)
{
  JS::Rooted<JS::PropertyDescriptor> desc(aCx);
  if (!JS_GetOwnPropertyDescriptor(aCx, aObject, aName, &desc)) return false;
  aValue.set(desc.object() ? desc.value() : JS::UndefinedValue());
  return true;
}

static already_AddRefed<nsIURI> URLLike(const nsAString& aSpecifier, nsIURI* aBase)
{
  nsCOMPtr<nsIURI> uri;
  bool relative = StringBeginsWith(aSpecifier, NS_LITERAL_STRING("/")) ||
                  StringBeginsWith(aSpecifier, NS_LITERAL_STRING("./")) ||
                  StringBeginsWith(aSpecifier, NS_LITERAL_STRING("../"));
  nsresult rv = NS_NewURI(getter_AddRefs(uri), aSpecifier, nullptr, relative ? aBase : nullptr);
  return NS_SUCCEEDED(rv) ? uri.forget() : nullptr;
}

static nsCString Serialize(nsIURI* aURI)
{
  nsCString result;
  if (aURI) aURI->GetSpec(result);
  return result;
}

static bool Special(nsIURI* aURI)
{
  nsAutoCString scheme;
  aURI->GetScheme(scheme);
  return scheme.EqualsLiteral("ftp") || scheme.EqualsLiteral("file") ||
         scheme.EqualsLiteral("http") || scheme.EqualsLiteral("https") ||
         scheme.EqualsLiteral("ws") || scheme.EqualsLiteral("wss");
}

static bool ScopeMatches(const nsACString& aScope, const nsACString& aBase)
{
  return aScope.Equals(aBase) || (StringEndsWith(aScope, NS_LITERAL_CSTRING("/")) &&
                                 StringBeginsWith(aBase, aScope));
}

bool ImportMap::ParseSpecifiers(JSContext* aCx, JS::HandleObject aObject,
                                 nsIURI* aBase, nsIDocument* aDocument,
                                 SpecifierMap& aResult)
{
  JS::Rooted<JS::IdVector> keys(aCx, JS::IdVector(aCx));
  if (!JS_Enumerate(aCx, aObject, &keys)) return false;
  for (size_t i = 0; i < keys.length(); ++i) {
    JS::RootedId id(aCx, keys[i]);
    nsAutoJSString key;
    if (!key.init(aCx, id)) return false;
    if (key.IsEmpty()) {
      Warn(aDocument, "Ignoring empty import-map specifier key.");
      continue;
    }
    nsString normalized(key);
    nsCOMPtr<nsIURI> keyURL = URLLike(key, aBase);
    if (keyURL) normalized = NS_ConvertUTF8toUTF16(Serialize(keyURL));
    JS::RootedValue value(aCx);
    if (!JS_GetPropertyById(aCx, aObject, id, &value)) return false;
    nsCOMPtr<nsIURI> address;
    if (value.isString()) {
      nsAutoJSString text;
      if (!text.init(aCx, value)) return false;
      address = URLLike(text, aBase);
      if (address && StringEndsWith(key, NS_LITERAL_STRING("/")) &&
          !StringEndsWith(Serialize(address), NS_LITERAL_CSTRING("/")))
        address = nullptr;
    }
    if (!address) Warn(aDocument, "Import-map address is invalid; matching imports will be blocked.");
    auto found = std::find_if(aResult.begin(), aResult.end(),
      [&](const Entry& entry) { return entry.key.Equals(normalized); });
    // Different source keys may normalize to the same URL. JSON/source order
    // wins within one map; later registered maps cannot replace that entry.
    if (found != aResult.end()) found->address = address;
    else aResult.AppendElement(Entry{normalized, address});
  }
  std::sort(aResult.begin(), aResult.end(),
    [](const Entry& a, const Entry& b) { return Compare(a.key, b.key) > 0; });
  return true;
}

UniquePtr<ImportMap> ImportMap::Parse(JSContext* aCx, const nsAString& aText,
                                      nsIURI* aBase, nsIDocument* aDocument)
{
  JS::RootedValue parsed(aCx);
  if (!JS_ParseJSON(aCx, aText.BeginReading(), aText.Length(), &parsed)) return nullptr;
  if (!IsMap(aCx, parsed)) {
    if (!JS_IsExceptionPending(aCx)) TypeError(aCx, "Import map must be a JSON object.");
    return nullptr;
  }
  UniquePtr<ImportMap> result(new ImportMap());
  JS::RootedObject object(aCx, &parsed.toObject());
  JS::RootedValue value(aCx);
  if (!OwnProperty(aCx, object, "imports", &value)) return nullptr;
  if (!value.isUndefined()) {
    if (!IsMap(aCx, value)) {
      if (!JS_IsExceptionPending(aCx)) TypeError(aCx, "Import-map imports must be an object.");
      return nullptr;
    }
    JS::RootedObject imports(aCx, &value.toObject());
    if (!ParseSpecifiers(aCx, imports, aBase, aDocument, result->mImports)) return nullptr;
  }
  if (!OwnProperty(aCx, object, "scopes", &value)) return nullptr;
  if (!value.isUndefined()) {
    if (!IsMap(aCx, value)) {
      if (!JS_IsExceptionPending(aCx)) TypeError(aCx, "Import-map scopes must be an object.");
      return nullptr;
    }
    JS::RootedObject scopes(aCx, &value.toObject());
    JS::Rooted<JS::IdVector> keys(aCx, JS::IdVector(aCx));
    if (!JS_Enumerate(aCx, scopes, &keys)) return nullptr;
    for (size_t i = 0; i < keys.length(); ++i) {
      JS::RootedId id(aCx, keys[i]);
      nsAutoJSString key;
      if (!key.init(aCx, id) || !JS_GetPropertyById(aCx, scopes, id, &value)) return nullptr;
      if (!IsMap(aCx, value)) {
        if (!JS_IsExceptionPending(aCx)) TypeError(aCx, "Each import-map scope must be an object.");
        return nullptr;
      }
      nsCOMPtr<nsIURI> uri;
      if (NS_FAILED(NS_NewURI(getter_AddRefs(uri), key, nullptr, aBase))) {
        Warn(aDocument, "Ignoring invalid import-map scope URL.");
        continue;
      }
      JS::RootedObject imports(aCx, &value.toObject());
      SpecifierMap normalized;
      if (!ParseSpecifiers(aCx, imports, aBase, aDocument, normalized)) return nullptr;
      nsCString scopeURL = Serialize(uri);
      auto found = std::find_if(result->mScopes.begin(), result->mScopes.end(),
        [&](const Scope& scope) { return scope.key.Equals(scopeURL); });
      if (found != result->mScopes.end()) found->imports = Move(normalized);
      else result->mScopes.AppendElement(Scope{scopeURL, Move(normalized)});
    }
    std::sort(result->mScopes.begin(), result->mScopes.end(),
      [](const Scope& a, const Scope& b) { return Compare(a.key, b.key) > 0; });
  }
  if (!OwnProperty(aCx, object, "integrity", &value)) return nullptr;
  if (!value.isUndefined()) {
    if (!IsMap(aCx, value)) {
      if (!JS_IsExceptionPending(aCx)) TypeError(aCx, "Import-map integrity must be an object.");
      return nullptr;
    }
    JS::RootedObject integrity(aCx, &value.toObject());
    JS::Rooted<JS::IdVector> keys(aCx, JS::IdVector(aCx));
    if (!JS_Enumerate(aCx, integrity, &keys)) return nullptr;
    for (size_t i = 0; i < keys.length(); ++i) {
      JS::RootedId id(aCx, keys[i]);
      nsAutoJSString key;
      if (!key.init(aCx, id) || !JS_GetPropertyById(aCx, integrity, id, &value)) return nullptr;
      nsCOMPtr<nsIURI> uri = URLLike(key, aBase);
      if (!uri || !value.isString()) {
        Warn(aDocument, "Ignoring invalid import-map integrity entry.");
        continue;
      }
      nsAutoJSString metadata;
      if (!metadata.init(aCx, value)) return nullptr;
      nsCString url = Serialize(uri);
      auto found = std::find_if(result->mIntegrity.begin(), result->mIntegrity.end(),
        [&](const Integrity& entry) { return entry.url.Equals(url); });
      if (found != result->mIntegrity.end()) found->metadata = metadata;
      else result->mIntegrity.AppendElement(Integrity{url, nsString(metadata)});
    }
  }
  JS::Rooted<JS::IdVector> keys(aCx, JS::IdVector(aCx));
  if (!JS_Enumerate(aCx, object, &keys)) return nullptr;
  for (size_t i = 0; i < keys.length(); ++i) {
    JS::RootedId id(aCx, keys[i]);
    nsAutoJSString key;
    if (!key.init(aCx, id)) return nullptr;
    if (!key.EqualsLiteral("imports") && !key.EqualsLiteral("scopes") && !key.EqualsLiteral("integrity"))
      Warn(aDocument, "Ignoring unknown top-level import-map member.");
  }
  return result;
}

nsresult ImportMap::Match(const SpecifierMap& aMap, const nsAString& aKey,
                           bool aPrefixAllowed, nsIURI** aResult)
{
  *aResult = nullptr;
  for (const auto& entry : aMap) {
    bool exact = entry.key.Equals(aKey);
    bool prefix = aPrefixAllowed && StringEndsWith(entry.key, NS_LITERAL_STRING("/")) &&
                  StringBeginsWith(aKey, entry.key);
    if (!exact && !prefix) continue;
    if (!entry.address) return NS_ERROR_DOM_TYPE_ERR;
    nsCOMPtr<nsIURI> uri = entry.address;
    if (!exact) {
      nsresult rv = NS_NewURI(getter_AddRefs(uri), Substring(aKey, entry.key.Length()),
                               nullptr, entry.address);
      if (NS_FAILED(rv) || !StringBeginsWith(Serialize(uri), Serialize(entry.address)))
        return NS_ERROR_DOM_TYPE_ERR; // no scope/global/URL fallback after a match
    }
    uri.forget(aResult);
    return NS_OK;
  }
  return NS_OK;
}

already_AddRefed<nsIURI> ImportMap::Resolve(const nsAString& aSpecifier,
                                            nsIURI* aBase, bool aRecord)
{
  nsCOMPtr<nsIURI> asURL = URLLike(aSpecifier, aBase);
  nsString normalized = asURL ? NS_ConvertUTF8toUTF16(Serialize(asURL)) : nsString(aSpecifier);
  nsCString base = Serialize(aBase);
  bool prefixAllowed = !asURL || Special(asURL);
  nsCOMPtr<nsIURI> result;
  for (const auto& scope : mScopes) {
    if (!ScopeMatches(scope.key, base)) continue;
    if (NS_FAILED(Match(scope.imports, normalized, prefixAllowed, getter_AddRefs(result))))
      return nullptr;
    if (result) break;
  }
  if (!result && NS_FAILED(Match(mImports, normalized, prefixAllowed, getter_AddRefs(result))))
    return nullptr;
  if (!result) result = asURL;
  if (result && aRecord) {
    Resolution record{base, normalized, prefixAllowed};
    auto less = [](const Resolution& a, const Resolution& b) {
      int32_t order = Compare(a.base, b.base);
      return order < 0 || (!order && Compare(a.specifier, b.specifier) < 0);
    };
    auto position = std::lower_bound(mResolved.begin(), mResolved.end(), record, less);
    if (position == mResolved.end() || !position->base.Equals(base) ||
        !position->specifier.Equals(normalized)) {
      size_t index = position - mResolved.begin();
      mResolved.InsertElementAt(index, Move(record));
    }
  }
  return result.forget();
}

void ImportMap::MergeSpecifiers(SpecifierMap& aOld, SpecifierMap& aNew,
                                 const nsACString* aScope, nsIDocument* aDocument)
{
  for (auto& entry : aNew) {
    bool ignored = std::any_of(aOld.begin(), aOld.end(),
      [&](const Entry& old) { return old.key.Equals(entry.key); });
    for (const auto& resolved : mResolved) {
      if (ignored) break;
      if (aScope) {
        if (!ScopeMatches(*aScope, resolved.base)) continue;
        ignored = entry.key.Equals(resolved.specifier) ||
          (resolved.prefixMatchAllowed && StringEndsWith(entry.key, NS_LITERAL_STRING("/")) &&
           StringBeginsWith(resolved.specifier, entry.key));
      } else {
        // HTML's imports merge protects previously resolved prefixes as well
        // as exact matches. This includes successful ordinary URL resolution
        // before the first import map was registered.
        ignored = StringBeginsWith(resolved.specifier, entry.key);
      }
    }
    if (ignored) Warn(aDocument, "Ignoring import-map entry conflicting with an existing map or resolution.");
    else aOld.AppendElement(Move(entry));
  }
  std::sort(aOld.begin(), aOld.end(),
    [](const Entry& a, const Entry& b) { return Compare(a.key, b.key) > 0; });
}

void ImportMap::Merge(ImportMap& aNewMap, nsIDocument* aDocument)
{
  for (auto& scope : aNewMap.mScopes) {
    auto found = std::find_if(mScopes.begin(), mScopes.end(),
      [&](const Scope& old) { return old.key.Equals(scope.key); });
    if (found == mScopes.end()) {
      Scope* added = mScopes.AppendElement();
      added->key = scope.key;
      MergeSpecifiers(added->imports, scope.imports, &added->key, aDocument);
    } else MergeSpecifiers(found->imports, scope.imports, &scope.key, aDocument);
  }
  std::sort(mScopes.begin(), mScopes.end(),
    [](const Scope& a, const Scope& b) { return Compare(a.key, b.key) > 0; });
  for (auto& integrity : aNewMap.mIntegrity) {
    bool exists = std::any_of(mIntegrity.begin(), mIntegrity.end(),
      [&](const Integrity& old) { return old.url.Equals(integrity.url); });
    if (!exists) mIntegrity.AppendElement(Move(integrity));
  }
  MergeSpecifiers(mImports, aNewMap.mImports, nullptr, aDocument);
}

bool ImportMap::GetIntegrity(nsIURI* aURI, nsAString& aResult) const
{
  nsCString url = Serialize(aURI);
  for (const auto& entry : mIntegrity) {
    if (entry.url.Equals(url)) {
      aResult = entry.metadata;
      return true;
    }
  }
  return false;
}

size_t ImportMap::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const
{
  size_t size = aMallocSizeOf(this) + mImports.ShallowSizeOfExcludingThis(aMallocSizeOf) +
    mScopes.ShallowSizeOfExcludingThis(aMallocSizeOf) + mIntegrity.ShallowSizeOfExcludingThis(aMallocSizeOf) +
    mResolved.ShallowSizeOfExcludingThis(aMallocSizeOf);
  auto mapSize = [&](const SpecifierMap& map) {
    size_t strings = 0;
    for (const auto& entry : map) strings += entry.key.SizeOfExcludingThisIfUnshared(aMallocSizeOf);
    return strings;
  };
  size += mapSize(mImports);
  for (const auto& scope : mScopes)
    size += scope.key.SizeOfExcludingThisIfUnshared(aMallocSizeOf) +
      scope.imports.ShallowSizeOfExcludingThis(aMallocSizeOf) + mapSize(scope.imports);
  for (const auto& entry : mIntegrity)
    size += entry.url.SizeOfExcludingThisIfUnshared(aMallocSizeOf) +
      entry.metadata.SizeOfExcludingThisIfUnshared(aMallocSizeOf);
  for (const auto& entry : mResolved)
    size += entry.base.SizeOfExcludingThisIfUnshared(aMallocSizeOf) +
      entry.specifier.SizeOfExcludingThisIfUnshared(aMallocSizeOf);
  return size;
}

} // namespace dom
} // namespace mozilla
