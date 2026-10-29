/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jsapi-tests/tests.h"
#include "jsarray.h"
#include "jscompartment.h"
#include "jsnum.h"
#include "vm/ArrayObject-inl.h"
#include "vm/String-inl.h"

BEGIN_TEST(testNurseryStrings)
{
    JS_SetNurseryStringsEnabled(cx, true, true);
    JS::RootedString a(cx, JS_NewStringCopyZ(cx, "duplicate ordinary string with malloc characters"));
    JS::RootedString b(cx, JS_NewStringCopyZ(cx, "duplicate ordinary string with malloc characters"));
    CHECK(a && b && a != b);
    CHECK(js::gc::IsInsideNursery(a.get()));
    auto zone = JS::GetStringZone(a);
    CHECK(zone);
    cx->minorGC(JS::gcreason::API);
    CHECK(!js::gc::IsInsideNursery(a.get()));
    CHECK(a == b);
    CHECK(JS::GetStringZone(a) == zone);

    // Bulk copies into tenured arrays must remember string-only ranges.
    JS::Rooted<js::ArrayObject*> array(cx, js::NewDenseFullyAllocatedArray(cx, 2, nullptr,
                                                                         js::TenuredObject));
    CHECK(array);
    JS::RootedString first(cx, JS_NewStringCopyZ(cx, "bulk copied ordinary string with malloc characters"));
    JS::RootedString second(cx, JS_NewStringCopyZ(cx, "another bulk copied ordinary string"));
    CHECK(first && second);
    JS::Value values[2] = { JS::StringValue(first), JS::StringValue(second) };
    array->setDenseInitializedLength(2);
    array->initDenseElements(0, values, 2);
    values[0].setUndefined();
    values[1].setUndefined();
    first = nullptr;
    second = nullptr;
    cx->minorGC(JS::gcreason::API);
    CHECK(!js::gc::IsInsideNursery(array->getDenseElement(0).toString()));
    bool bulkEqual;
    CHECK(JS_StringEqualsAscii(cx, array->getDenseElement(0).toString(),
                              "bulk copied ordinary string with malloc characters", &bulkEqual));
    CHECK(bulkEqual);

    // Identity-based native hashes must keep their unique IDs. Such strings
    // cannot be merged with another cell during deduplication.
    a = JS_NewStringCopyZ(cx, "unique identity ordinary string with malloc characters");
    b = JS_NewStringCopyZ(cx, "unique identity ordinary string with malloc characters");
    uint64_t uid;
    CHECK(zone->getUniqueId(a, &uid));
    cx->minorGC(JS::gcreason::API);
    CHECK(a != b);
    CHECK(zone->getUniqueIdInfallible(a) == uid);

    // Exposing malloc-backed chars across GC must inhibit deduplication.
    a = JS_NewStringCopyZ(cx, "stable ordinary string with malloc characters");
    b = JS_NewStringCopyZ(cx, "stable ordinary string with malloc characters");
    js::AutoStableStringChars stable(cx);
    CHECK(stable.init(cx, a));
    const JS::Latin1Char* chars = stable.latin1Range().begin().get();
    cx->minorGC(JS::gcreason::API);
    CHECK(a != b);
    CHECK(stable.latin1Range().begin().get() == chars);
    CHECK(!memcmp(chars, "stable ordinary string with malloc characters", a->length()));

    // A dependent base must keep its own chars, regardless of root trace order.
    a = JS_NewStringCopyZ(cx, "dependent base ordinary string with malloc characters");
    b = JS_NewStringCopyZ(cx, "dependent base ordinary string with malloc characters");
    JS::RootedString dep(cx, JS_NewDependentString(cx, a, 3, 43));
    CHECK(dep);
    cx->minorGC(JS::gcreason::API);
    CHECK(a != b);
    bool equal;
    CHECK(JS_StringEqualsAscii(cx, dep, "endent base ordinary string with malloc cha", &equal));
    CHECK(equal);
    JS_SetNurseryStringsEnabled(cx, false, false);
    CHECK(!js::gc::IsInsideNursery(dep.get()));
    return true;
}
END_TEST(testNurseryStrings)

BEGIN_TEST(testNurseryStrings_DtoaCache)
{
    JS_SetNurseryStringsEnabled(cx, true, false);
    auto& cache = cx->compartment()->dtoaCache;
    cache.purge();

    // Ordinary number strings can now move. A weak cache must follow a live
    // entry without promoting an otherwise dead number string.
    JS::RootedString number(cx, js::Int32ToString<js::CanGC>(cx, 1900));
    CHECK(number);
    CHECK(js::gc::IsInsideNursery(number.get()));
    CHECK(cache.lookup(10, 1900) == number.get());
    cx->minorGC(JS::gcreason::API);
    CHECK(!js::gc::IsInsideNursery(number.get()));
    CHECK(cache.lookup(10, 1900) == number.get());
    cx->minorGC(JS::gcreason::API);
    CHECK(cache.lookup(10, 1900) == number.get());

    // Don't leave the dead cell rooted through a C++ local or the value stack.
    number = js::Int32ToString<js::CanGC>(cx, 1901);
    CHECK(number);
    CHECK(js::gc::IsInsideNursery(number.get()));
    number = nullptr;
    cx->minorGC(JS::gcreason::API);
    CHECK(cache.lookup(10, 1901) == nullptr);

    // Disabling future nursery allocation also collects existing cached cells.
    number = js::Int32ToString<js::CanGC>(cx, 1902);
    CHECK(number);
    JS_SetNurseryStringsEnabled(cx, false, false);
    CHECK(!js::gc::IsInsideNursery(number.get()));
    CHECK(cache.lookup(10, 1902) == number.get());
    return true;
}
END_TEST(testNurseryStrings_DtoaCache)

BEGIN_TEST(testNurseryStrings_EvalCache)
{
    JS_SetNurseryStringsEnabled(cx, true, false);
    cx->caches.evalCache.clear();
    EXEC("function nurseryEval(source) { return eval(source); }");
    JS::RootedString source(cx, JS_NewStringCopyZ(cx, "40 + 2"));
    CHECK(source);
    JS::RootedValue argument(cx, JS::StringValue(source));
    JS::RootedValue result(cx);
    CHECK(JS_CallFunctionName(cx, global, "nurseryEval",
                              JS::HandleValueArray(argument), &result));
    CHECK(result.isInt32() && result.toInt32() == 42);
    CHECK(cx->caches.evalCache.count() == 1);
    CHECK(cx->caches.evalCache.all().front().str == source.get());
    CHECK(js::gc::IsInsideNursery(source.get()));
    cx->minorGC(JS::gcreason::API);
    CHECK(cx->caches.evalCache.count() == 1);
    CHECK(cx->caches.evalCache.all().front().str == source.get());

    source = JS_NewStringCopyZ(cx, "41 + 1");
    argument.setString(source);
    CHECK(JS_CallFunctionName(cx, global, "nurseryEval",
                              JS::HandleValueArray(argument), &result));
    CHECK(cx->caches.evalCache.count() == 2);
    source = nullptr;
    argument.setUndefined();
    cx->minorGC(JS::gcreason::API);
    CHECK(cx->caches.evalCache.count() == 1);
    JS_SetNurseryStringsEnabled(cx, false, false);
    return true;
}
END_TEST(testNurseryStrings_EvalCache)
