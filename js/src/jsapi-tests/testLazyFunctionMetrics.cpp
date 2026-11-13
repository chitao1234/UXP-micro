/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jsapi-tests/tests.h"

#include "js/MemoryMetrics.h"
#include "jsfun.h"
#include "jsscript.h"

// Primary sizes need not use a real allocator here: the native test checks
// classification, GC cell bytes and non-mutating traversal. The browser probe
// records real table sizes alongside the owning report.
static size_t LazyGraphNoMallocSizeOf(const void*) { return 0; }

class LazyGraphRuntimeStats : public JS::RuntimeStats
{
  public:
    LazyGraphRuntimeStats() : JS::RuntimeStats(LazyGraphNoMallocSizeOf) {}
    void initExtraCompartmentStats(JSCompartment*, JS::CompartmentStats*) override {}
    void initExtraZoneStats(JS::Zone* zone, JS::ZoneStats* stats) override {
        stats->extra = zone;
    }

    const JS::LazyFunctionInfo* find(JS::Zone* zone) const {
        for (const auto& stats : zoneStatsVector) {
            if (stats.extra == zone)
                return &stats.lazyFunctions;
        }
        return nullptr;
    }
};

BEGIN_TEST(testLazyFunctionMetrics)
{
    LazyGraphRuntimeStats before;
    CHECK(JS::CollectRuntimeStats(cx, &before, nullptr, true));
    const auto* initial = before.find(global->zone());
    CHECK(initial);
    const size_t initialNested = initial->nestedFunctionCount;
    const size_t initialLazy = initial->nestedLazyScriptCount;

    EXEC("function graphOuter(a) {"
         "  return function graphMiddle(b) {"
         "    function graphLeaf(c) { return a + b + c; }"
         "    return graphLeaf;"
         "  };"
         "}");
    JS::RootedValue outerValue(cx);
    CHECK(JS_GetProperty(cx, global, "graphOuter", &outerValue));
    JS::RootedFunction outer(cx, &outerValue.toObject().as<JSFunction>());
    CHECK(outer->isInterpretedLazy());
    JS::Rooted<js::LazyScript*> lazy(cx, outer->lazyScript());
    CHECK_EQUAL(lazy->numInnerFunctions(), 1u);
    CHECK(lazy->sourceObject());
    JS::RootedFunction middle(cx, lazy->innerFunctions()[0]);
    CHECK(middle->isInterpretedLazy());
    CHECK(!middle->lazyScript()->sourceObject());

    LazyGraphRuntimeStats uncompiled;
    CHECK(JS::CollectRuntimeStats(cx, &uncompiled, nullptr, true));
    const auto* nested = uncompiled.find(global->zone());
    CHECK(nested);
    CHECK_EQUAL(nested->nestedFunctionCount, initialNested + 2);
    CHECK_EQUAL(nested->nestedLazyScriptCount, initialLazy + 2);
    CHECK(nested->nestedFunctionGCHeap > initial->nestedFunctionGCHeap);
    CHECK(nested->nestedLazyScriptGCHeap > initial->nestedLazyScriptGCHeap);
    CHECK_EQUAL(nested->nestedInnerFunctions, initial->nestedInnerFunctions + 1);
    CHECK_EQUAL(nested->nestedCloneCount, size_t(0));
    CHECK(outer->isInterpretedLazy()); // Reporting must not delazify.
    CHECK(middle->isInterpretedLazy());
    CHECK(!middle->lazyScript()->sourceObject());

    LazyGraphRuntimeStats repeated;
    CHECK(JS::CollectRuntimeStats(cx, &repeated, nullptr, true));
    const auto* again = repeated.find(global->zone());
    CHECK(again);
    CHECK_EQUAL(again->functionCount, nested->functionCount);
    CHECK_EQUAL(again->nestedFunctionCount, nested->nestedFunctionCount);
    CHECK_EQUAL(again->nestedLazyScriptCount, nested->nestedLazyScriptCount);

    // Compiling an enclosing function assigns scope/source only to its
    // immediate children. Its still-lazy child's descendant remains a candidate.
    EXEC("var graphMiddle = graphOuter(4);");
    LazyGraphRuntimeStats partial;
    CHECK(JS::CollectRuntimeStats(cx, &partial, nullptr, true));
    const auto* remaining = partial.find(global->zone());
    CHECK(remaining);
    CHECK_EQUAL(remaining->nestedFunctionCount, initialNested + 1);
    CHECK_EQUAL(remaining->nestedLazyScriptCount, initialLazy + 1);

    EXEC("var graphLeaf = graphMiddle(5);"
         "if (graphLeaf(6) !== 15) throw new Error('closure binding');");
    LazyGraphRuntimeStats compiled;
    CHECK(JS::CollectRuntimeStats(cx, &compiled, nullptr, true));
    const auto* final = compiled.find(global->zone());
    CHECK(final);
    CHECK_EQUAL(final->nestedFunctionCount, initialNested);
    CHECK_EQUAL(final->nestedLazyScriptCount, initialLazy);
    CHECK_EQUAL(final->nestedCloneCount, size_t(0));

    // Zone totals remain a separate overlapping view. They must not change
    // the owned-byte arithmetic used by RuntimeStats.
    JS::LazyFunctionInfo total;
    for (const auto& stats : compiled.zoneStatsVector)
        total.add(stats.lazyFunctions);
    CHECK_EQUAL(total.functionCount, compiled.zTotals.lazyFunctions.functionCount);
    CHECK_EQUAL(total.nestedLazyScriptGCHeap,
                compiled.zTotals.lazyFunctions.nestedLazyScriptGCHeap);
    return true;
}
END_TEST(testLazyFunctionMetrics)
