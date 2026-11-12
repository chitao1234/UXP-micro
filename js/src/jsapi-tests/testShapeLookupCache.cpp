/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jsapi-tests/tests.h"
#include "vm/Shape-inl.h"
#include "vm/NativeObject-inl.h"

using namespace js;

struct AutoShapeLookupPolicy {
    bool previous = ShapeLookupCacheEnabled();
    AutoShapeLookupPolicy() { SetShapeLookupCacheEnabled(true); }
    ~AutoShapeLookupPolicy() { SetShapeLookupCacheEnabled(previous); }
};

BEGIN_TEST(testShapeLookupCache)
{
    AutoShapeLookupPolicy policy;
    RootedValue value(cx);
    EVAL("var shapeCacheObject = {}; for (var i=0;i<80;i++) shapeCacheObject['p'+i]=i;"
         "shapeCacheObject;", &value);
    Rooted<NativeObject*> obj(cx, &value.toObject().as<NativeObject>());
    CHECK(!obj->inDictionaryMode());
    RootedShape shape(cx, obj->lastProperty());
    RootedString name(cx, JS_AtomizeString(cx, "p20"));
    RootedId id(cx);
    CHECK(JS_StringToId(cx, name, &id));
    for (unsigned i = 0; i < 16; ++i)
        CHECK(obj->lookup(cx, id));
    CHECK(shape->hasIC());
    CHECK(!shape->hasTable());

    // Cached misses must keep unique symbol keys alive even though those keys
    // are absent from the shape lineage. Keep the cache across the GC purge.
    RootedSymbol symbol(cx, JS::NewSymbol(cx, nullptr));
    CHECK(symbol);
    JS::Symbol* cachedSymbol = symbol;
    id = SYMBOL_TO_JSID(symbol);
    CHECK(!obj->lookup(cx, id));
    symbol = nullptr;
    id = JSID_VOID;
    {
        AutoKeepShapeTables keep(cx);
        JS_GC(cx);
        CHECK(shape->hasIC());
        CHECK(cachedSymbol->isMarked());
        symbol = cachedSymbol;
        id = SYMBOL_TO_JSID(symbol);
        CHECK(!obj->lookup(cx, id));
        JS::PrepareForFullGC(cx);
        JS::GCForReason(cx, GC_SHRINK, JS::gcreason::API);
        CHECK(shape->hasIC());
        id = SYMBOL_TO_JSID(symbol);
        CHECK(!obj->lookup(cx, id));
        name = JS_AtomizeString(cx, "p20");
        CHECK(name);
        CHECK(JS_StringToId(cx, name, &id));
        CHECK(obj->lookup(cx, id));
        CHECK(obj->lookupPure(id));
    }

    // Distinct lookups exhaust the IC and promote to a full table. Every
    // property must still be discoverable, including the uncached keys.
    for (unsigned i = 0; i < 20; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "p%u", i);
        name = JS_AtomizeString(cx, key);
        CHECK(name && JS_StringToId(cx, name, &id));
        CHECK(obj->lookup(cx, id));
    }
    CHECK(shape->hasTable() && !shape->hasIC());
    EXEC("delete shapeCacheObject.p25;"
         "Object.defineProperty(shapeCacheObject,'p26',{get(){return 260;},configurable:true});"
         "shapeCacheObject.p25=250;"
         "if(shapeCacheObject.p25!==250 || shapeCacheObject.p26!==260) throw 'mutation';"
         "if(Object.keys(shapeCacheObject).pop()!=='p25') throw 'enumeration';");
    CHECK(obj->inDictionaryMode());
    CHECK(obj->lastProperty()->hasTable());
    CHECK(!obj->lastProperty()->hasIC());
    return true;
}
END_TEST(testShapeLookupCache)

BEGIN_TEST(testShapeLookupCachePolicyTransition)
{
    AutoShapeLookupPolicy policy;
    RootedValue value(cx);
    EVAL("var transitionObject={};for(var i=0;i<64;i++)transitionObject['t'+i]=i;transitionObject;", &value);
    Rooted<NativeObject*> obj(cx, &value.toObject().as<NativeObject>());
    RootedShape shape(cx, obj->lastProperty());
    RootedString key(cx, JS_AtomizeString(cx, "t10"));
    RootedId id(cx);
    CHECK(key && JS_StringToId(cx, key, &id));
    for (unsigned i = 0; i < 16; ++i)
        CHECK(obj->lookup(cx, id));
    CHECK(shape->hasIC());
    SetShapeLookupCacheEnabled(false);
    CHECK(obj->lookup(cx, id));
    CHECK(shape->hasTable() && !shape->hasIC());
    SetShapeLookupCacheEnabled(true);
    CHECK(obj->lookup(cx, id));
    CHECK(shape->hasTable()); // Policy changes do not invalidate existing data.
    return true;
}
END_TEST(testShapeLookupCachePolicyTransition)
