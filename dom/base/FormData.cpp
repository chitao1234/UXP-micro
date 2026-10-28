/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FormData.h"
#include "nsIVariant.h"
#include "nsIInputStream.h"
#include "mozilla/dom/File.h"
#include "mozilla/dom/Directory.h"
#include "mozilla/dom/HTMLFormElement.h"
#include "nsIFormControl.h"
#include "nsQueryObject.h"
#include "prtime.h"

#include "MultipartBlobImpl.h"

using namespace mozilla;
using namespace mozilla::dom;

FormData::FormData(nsISupports* aOwner)
  : FormData(aOwner, NS_LITERAL_CSTRING("UTF-8"), nullptr)
{
}

FormData::FormData(nsISupports* aOwner, const nsACString& aCharset,
                   nsIContent* aSubmitter)
  : HTMLFormSubmission(aCharset, aSubmitter)
  , mOwner(aOwner)
{
}

namespace {

already_AddRefed<File>
GetOrCreateFileCalledBlob(Blob& aBlob, ErrorResult& aRv)
{
  // If this is file, we can just use it
  RefPtr<File> file = aBlob.ToFile();
  if (file) {
    return file.forget();
  }

  // Forcing 'blob' as filename
  file = aBlob.ToFile(NS_LITERAL_STRING("blob"), aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  return file.forget();
}

already_AddRefed<File>
GetBlobForFormDataStorage(Blob& aBlob, const Optional<nsAString>& aFilename,
                          ErrorResult& aRv)
{
  // Forcing a filename
  if (aFilename.WasPassed()) {
    RefPtr<File> file = aBlob.ToFile(aFilename.Value(), aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return nullptr;
    }

    return file.forget();
  }

  return GetOrCreateFileCalledBlob(aBlob, aRv);
}

} // namespace

// -------------------------------------------------------------------------
// nsISupports

NS_IMPL_CYCLE_COLLECTION_CLASS(FormData)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(FormData)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mOwner)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mOriginatingElement)

  for (uint32_t i = 0, len = tmp->mFormData.Length(); i < len; ++i) {
    ImplCycleCollectionUnlink(tmp->mFormData[i].value);
  }

  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(FormData)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOwner)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOriginatingElement)

  for (uint32_t i = 0, len = tmp->mFormData.Length(); i < len; ++i) {
    ImplCycleCollectionTraverse(cb, tmp->mFormData[i].value,
                                "mFormData[i].GetAsBlob()", 0);
  }

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_WRAPPERCACHE(FormData)

NS_IMPL_CYCLE_COLLECTING_ADDREF(FormData)
NS_IMPL_CYCLE_COLLECTING_RELEASE(FormData)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(FormData)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsIDOMFormData)
  NS_INTERFACE_MAP_ENTRY(nsIXHRSendable)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIDOMFormData)
NS_INTERFACE_MAP_END

// -------------------------------------------------------------------------
// HTMLFormSubmission
nsresult
FormData::GetEncodedSubmission(nsIURI* aURI,
                               nsIInputStream** aPostDataStream)
{
  NS_NOTREACHED("Shouldn't call FormData::GetEncodedSubmission");
  return NS_OK;
}

void
FormData::Append(const nsAString& aName, const nsAString& aValue,
                 ErrorResult& aRv)
{
  AddNameValuePair(aName, aValue);
}

void
FormData::Append(const nsAString& aName, Blob& aBlob,
                 const Optional<nsAString>& aFilename,
                 ErrorResult& aRv)
{
  RefPtr<File> file = GetBlobForFormDataStorage(aBlob, aFilename, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return;
  }

  AddNameBlobOrNullPair(aName, file);
}

void
FormData::Append(const nsAString& aName, Directory* aDirectory)
{
  AddNameDirectoryPair(aName, aDirectory);
}

void
FormData::Delete(const nsAString& aName)
{
  // We have to use this slightly awkward for loop since uint32_t >= 0 is an
  // error for being always true.
  for (uint32_t i = mFormData.Length(); i-- > 0; ) {
    if (aName.Equals(mFormData[i].name)) {
      mFormData.RemoveElementAt(i);
    }
  }
}

void
FormData::Get(const nsAString& aName,
              Nullable<OwningBlobOrDirectoryOrUSVString>& aOutValue)
{
  for (uint32_t i = 0; i < mFormData.Length(); ++i) {
    if (aName.Equals(mFormData[i].name)) {
      aOutValue.SetValue() = mFormData[i].value;
      return;
    }
  }

  aOutValue.SetNull();
}

void
FormData::GetAll(const nsAString& aName,
                 nsTArray<OwningBlobOrDirectoryOrUSVString>& aValues)
{
  for (uint32_t i = 0; i < mFormData.Length(); ++i) {
    if (aName.Equals(mFormData[i].name)) {
      OwningBlobOrDirectoryOrUSVString* element = aValues.AppendElement();
      *element = mFormData[i].value;
    }
  }
}

bool
FormData::Has(const nsAString& aName)
{
  for (uint32_t i = 0; i < mFormData.Length(); ++i) {
    if (aName.Equals(mFormData[i].name)) {
      return true;
    }
  }

  return false;
}

nsresult
FormData::AddNameBlobOrNullPair(const nsAString& aName, Blob* aBlob)
{
  RefPtr<File> file;

  if (!aBlob) {
    RefPtr<BlobImpl> empty = new EmptyBlobImpl(EmptyString(),
        NS_LITERAL_STRING("application/octet-stream"), PR_Now());
    file = File::Create(mOwner, empty);
  } else {
    ErrorResult rv;
    file = GetOrCreateFileCalledBlob(*aBlob, rv);
    if (NS_WARN_IF(rv.Failed())) {
      return rv.StealNSResult();
    }
  }
  nsAutoString name(aName);
  NormalizeUSVString(name);
  FormDataTuple* data = mFormData.AppendElement();
  SetNameFilePair(data, name, file);
  return NS_OK;
}

nsresult
FormData::AddNameDirectoryPair(const nsAString& aName, Directory* aDirectory)
{
  MOZ_ASSERT(aDirectory);
  nsAutoString name(aName);
  NormalizeUSVString(name);
  FormDataTuple* data = mFormData.AppendElement();
  SetNameDirectoryPair(data, name, aDirectory);
  return NS_OK;
}

FormData::FormDataTuple*
FormData::RemoveAllOthersAndGetFirstFormDataTuple(const nsAString& aName)
{
  FormDataTuple* lastFoundTuple = nullptr;
  uint32_t lastFoundIndex = mFormData.Length();
  // We have to use this slightly awkward for loop since uint32_t >= 0 is an
  // error for being always true.
  for (uint32_t i = mFormData.Length(); i-- > 0; ) {
    if (aName.Equals(mFormData[i].name)) {
      if (lastFoundTuple) {
        // The one we found earlier was not the first one, we can remove it.
        mFormData.RemoveElementAt(lastFoundIndex);
      }

      lastFoundTuple = &mFormData[i];
      lastFoundIndex = i;
    }
  }

  return lastFoundTuple;
}

void
FormData::Set(const nsAString& aName, Blob& aBlob,
              const Optional<nsAString>& aFilename,
              ErrorResult& aRv)
{
  FormDataTuple* tuple = RemoveAllOthersAndGetFirstFormDataTuple(aName);
  if (tuple) {
    RefPtr<File> file = GetBlobForFormDataStorage(aBlob, aFilename, aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return;
    }

    SetNameFilePair(tuple, aName, file);
  } else {
    Append(aName, aBlob, aFilename, aRv);
  }
}

void
FormData::Set(const nsAString& aName, const nsAString& aValue,
              ErrorResult& aRv)
{
  FormDataTuple* tuple = RemoveAllOthersAndGetFirstFormDataTuple(aName);
  if (tuple) {
    SetNameValuePair(tuple, aName, aValue);
  } else {
    Append(aName, aValue, aRv);
  }
}

uint32_t
FormData::GetIterableLength() const
{
  return mFormData.Length();
}

const nsAString&
FormData::GetKeyAtIndex(uint32_t aIndex) const
{
  MOZ_ASSERT(aIndex < mFormData.Length());
  return mFormData[aIndex].name;
}

const OwningBlobOrDirectoryOrUSVString&
FormData::GetValueAtIndex(uint32_t aIndex) const
{
  MOZ_ASSERT(aIndex < mFormData.Length());
  return mFormData[aIndex].value;
}

void
FormData::SetNameValuePair(FormDataTuple* aData,
                           const nsAString& aName,
                           const nsAString& aValue)
{
  MOZ_ASSERT(aData);
  aData->name = aName;
  aData->value.SetAsUSVString() = aValue;
}

void
FormData::SetNameFilePair(FormDataTuple* aData,
                          const nsAString& aName,
                          File* aFile)
{
  MOZ_ASSERT(aData);
  MOZ_ASSERT(aFile);

  aData->name = aName;
  aData->value.SetAsBlob() = aFile;
}

void
FormData::SetNameDirectoryPair(FormDataTuple* aData,
                               const nsAString& aName,
                               Directory* aDirectory)
{
  MOZ_ASSERT(aData);
  MOZ_ASSERT(aDirectory);

  aData->name = aName;
  aData->value.SetAsDirectory() = aDirectory;
}

// -------------------------------------------------------------------------
// nsIDOMFormData

NS_IMETHODIMP
FormData::Append(const nsAString& aName, nsIVariant* aValue)
{
  uint16_t dataType;
  nsresult rv = aValue->GetDataType(&dataType);
  NS_ENSURE_SUCCESS(rv, rv);

  if (dataType == nsIDataType::VTYPE_INTERFACE ||
      dataType == nsIDataType::VTYPE_INTERFACE_IS) {
    nsCOMPtr<nsISupports> supports;
    nsID *iid;
    rv = aValue->GetAsInterface(&iid, getter_AddRefs(supports));
    NS_ENSURE_SUCCESS(rv, rv);

    free(iid);

    nsCOMPtr<nsIDOMBlob> domBlob = do_QueryInterface(supports);
    RefPtr<Blob> blob = static_cast<Blob*>(domBlob.get());
    if (domBlob) {
      Optional<nsAString> temp;
      ErrorResult rv;
      Append(aName, *blob, temp, rv);
      if (NS_WARN_IF(rv.Failed())) {
        return rv.StealNSResult();
      }

      return NS_OK;
    }
  }

  char16_t* stringData = nullptr;
  uint32_t stringLen = 0;
  rv = aValue->GetAsWStringWithSize(&stringLen, &stringData);
  NS_ENSURE_SUCCESS(rv, rv);

  nsString valAsString;
  valAsString.Adopt(stringData, stringLen);

  ErrorResult error;
  Append(aName, valAsString, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }

  return NS_OK;
}

/* virtual */ JSObject*
FormData::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return FormDataBinding::Wrap(aCx, this, aGivenProto);
}

/* static */ already_AddRefed<FormData>
FormData::Constructor(const GlobalObject& aGlobal,
                      const Optional<NonNull<HTMLFormElement> >& aFormElement,
                      nsGenericHTMLElement* aSubmitter,
                      ErrorResult& aRv)
{
  if (aFormElement.WasPassed()) {
    if (aSubmitter) {
      nsCOMPtr<nsIFormControl> control = do_QueryObject(aSubmitter);
      if (!control || !control->IsSubmitControl()) {
        aRv.ThrowTypeError<MSG_NOT_SUBMIT_BUTTON>();
        return nullptr;
      }
      if (control->GetFormElement() != &aFormElement.Value()) {
        aRv.Throw(NS_ERROR_DOM_NOT_FOUND_ERR);
        return nullptr;
      }
    }
    RefPtr<FormData> formData = new FormData(aGlobal.GetAsSupports(),
        NS_LITERAL_CSTRING("UTF-8"), aSubmitter);
    aRv = aFormElement.Value().ConstructEntryList(formData);
    if (aRv.Failed()) {
      return nullptr;
    }
    // Event listeners may retain their FormData. The constructor adopts a
    // shallow clone of the entries, not the event's mutable entry list.
    return formData->Clone();
  }
  RefPtr<FormData> formData = new FormData(aGlobal.GetAsSupports());
  return formData.forget();
}

already_AddRefed<FormData>
FormData::Clone() const
{
  RefPtr<FormData> copy = new FormData(mOwner, mCharset, nullptr);
  copy->mFormData = mFormData;
  return copy.forget();
}

nsresult
FormData::CopySubmissionDataTo(HTMLFormSubmission* aSubmission) const
{
  for (const FormDataTuple& data : mFormData) {
    nsresult rv;
    if (data.value.IsUSVString()) {
      rv = aSubmission->AddNameValuePair(data.name, data.value.GetAsUSVString());
    } else if (data.value.IsBlob()) {
      rv = aSubmission->AddNameBlobOrNullPair(data.name, data.value.GetAsBlob());
    } else {
      MOZ_ASSERT(data.value.IsDirectory());
      rv = aSubmission->AddNameDirectoryPair(data.name, data.value.GetAsDirectory());
    }
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

// -------------------------------------------------------------------------
// nsIXHRSendable

NS_IMETHODIMP
FormData::GetSendInfo(nsIInputStream** aBody, uint64_t* aContentLength,
                      nsACString& aContentTypeWithCharset, nsACString& aCharset)
{
  FSMultipartFormData fs(NS_LITERAL_CSTRING("UTF-8"), nullptr);
  nsresult rv = CopySubmissionDataTo(&fs);
  NS_ENSURE_SUCCESS(rv, rv);

  fs.GetContentType(aContentTypeWithCharset);
  aCharset.Truncate();
  *aContentLength = 0;
  NS_ADDREF(*aBody = fs.GetSubmissionBody(aContentLength));

  return NS_OK;
}
