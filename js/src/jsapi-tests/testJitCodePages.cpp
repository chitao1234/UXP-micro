/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jsapi-tests/tests.h"

#include "gc/Memory.h"
#include "jit/ExecutableAllocator.h"
#include "jit/IonCode.h"
#include "jit/JitCompartment.h"
#include "js/MemoryMetrics.h"

#ifdef XP_WIN
# include "jswin.h"
#endif

using namespace js;
using namespace js::jit;

struct AutoJitCodePagePolicy {
    bool previous = JitCodeDecommitEnabled();
    explicit AutoJitCodePagePolicy(bool enabled) { SetJitCodeDecommitEnabled(enabled); }
    ~AutoJitCodePagePolicy() { SetJitCodeDecommitEnabled(previous); }
};

static size_t JitPageNoMallocSizeOf(const void*) { return 0; }

static JS::CodeSizes JitPageSizes(ExecutableAllocator& allocator)
{
    JS::CodeSizes sizes;
    allocator.addSizeOfCode(&sizes, JitPageNoMallocSizeOf);
    return sizes;
}

BEGIN_TEST(testJitCodePages)
{
    CHECK(cx->getJitRuntime(cx));
    AutoJitCodePagePolicy policy(true);
    ExecutableAllocator allocator(cx);
    const size_t page = gc::SystemPageSize();
    // The middle allocation shares its first page with live code, owns two
    // complete pages, and ends immediately before another live allocation.
    ExecutablePool *pa, *pb, *pc;
    auto a = static_cast<char*>(allocator.alloc(page / 2, &pa, OTHER_CODE));
    auto b = static_cast<char*>(allocator.alloc(5 * page / 2, &pb, OTHER_CODE));
    auto c = static_cast<char*>(allocator.alloc(page / 2, &pc, OTHER_CODE));
    CHECK(a && b && c && pa == pb && pb == pc);
    CHECK(ExecutableAllocator::makeWritable(a, 7 * page / 2));
    memset(a, 0x5a, page / 2);
    memset(b, 0x66, 5 * page / 2);
    memset(c, 0x6b, page / 2);
    CHECK(ExecutableAllocator::makeExecutable(a, 7 * page / 2));

    auto before = JitPageSizes(allocator);
    CHECK_EQUAL(before.reserved, ExecutableCodePageSize);
    CHECK_EQUAL(before.decommitted, ExecutableCodePageSize - 4 * page);
    CHECK_EQUAL(before.reclaimable, size_t(0));

    JitPoisonRangeVector ranges;
    CHECK(ranges.append(JitPoisonRange(pb, b, 5 * page / 2, true)));
    pb->addRef();
    pb->release(5 * page / 2, OTHER_CODE);
    // Dropping the allocation reference must not retire pages before poisoning.
    CHECK_EQUAL(JitPageSizes(allocator).decommitted, before.decommitted);
    ExecutableAllocator::poisonCode(cx, ranges);
    ranges.clear();
    auto after = JitPageSizes(allocator);
    CHECK_EQUAL(after.decommitted, before.decommitted + 2 * page);
    CHECK_EQUAL(after.other, page);
    CHECK_EQUAL(after.unused, page);
    for (size_t i = 0; i < page / 2; i++) {
        CHECK_EQUAL(a[i], char(0x5a));
        CHECK_EQUAL(c[i], char(0x6b));
    }
#ifdef XP_WIN
    MEMORY_BASIC_INFORMATION mbi;
    CHECK(VirtualQuery(a + page, &mbi, sizeof(mbi)) == sizeof(mbi));
    CHECK_EQUAL(mbi.State, DWORD(MEM_RESERVE));
    CHECK(VirtualQuery(c, &mbi, sizeof(mbi)) == sizeof(mbi));
    CHECK_EQUAL(mbi.State, DWORD(MEM_COMMIT));
    CHECK_EQUAL(mbi.Protect, DWORD(PAGE_EXECUTE_READ));
#endif
    {
        JitRuntime::AutoPreventBackedgePatching apbp(cx);
        allocator.makeAllWritable();
        allocator.makeAllExecutable();
    }
    CHECK(ranges.append(JitPoisonRange(pa, a, page / 2, true)));
    CHECK(ranges.append(JitPoisonRange(pc, c, page / 2, true)));
    pa->addRef();
    pc->addRef();
    pa->release(page / 2, OTHER_CODE);
    pc->release(page / 2, OTHER_CODE);
    ExecutableAllocator::poisonCode(cx, ranges);
    ranges.clear();
    CHECK_EQUAL(JitPageSizes(allocator).decommitted, ExecutableCodePageSize);

    // Recommit a partially used tail page; the old addresses are never reused.
    ExecutablePool* pd;
    auto d = static_cast<char*>(allocator.alloc(page / 2, &pd, OTHER_CODE));
    CHECK(d == c + page / 2 && pd == pa);
    CHECK_EQUAL(JitPageSizes(allocator).decommitted, ExecutableCodePageSize - page);
    CHECK(ExecutableAllocator::makeWritable(d, page / 2));
    memset(d, 0x7c, page / 2);
    CHECK(ExecutableAllocator::makeExecutable(d, page / 2));
    CHECK_EQUAL(d[page / 2 - 1], char(0x7c));
    // Perf integration intentionally leaks code addresses. Such poison ranges
    // retain their page ownership and code-kind bytes after the queue drains.
    CHECK(ranges.append(JitPoisonRange(pd, d, page / 2, false)));
    pd->addRef();
    ExecutableAllocator::poisonCode(cx, ranges);
    ranges.clear();
    CHECK_EQUAL(JitPageSizes(allocator).other, page / 2);
    CHECK_EQUAL(JitPageSizes(allocator).decommitted, ExecutableCodePageSize - page);
    {
        JitRuntime::AutoPreventBackedgePatching apbp(cx);
        pd->retireCodeRange(d, page / 2);
    }
    pd->release(page / 2, OTHER_CODE);
    return true;
}
END_TEST(testJitCodePages)

BEGIN_TEST(testJitCodePagesPolicyAndOOM)
{
    CHECK(cx->getJitRuntime(cx));
    AutoJitCodePagePolicy policy(false);
    ExecutableAllocator allocator(cx);
    const size_t page = gc::SystemPageSize();
    ExecutablePool* pool;
    void* first = allocator.alloc(page, &pool, OTHER_CODE);
    CHECK(first);
    CHECK_EQUAL(JitPageSizes(allocator).reclaimable, ExecutableCodePageSize - page);
    CHECK_EQUAL(JitPageSizes(allocator).decommitted, size_t(0));
    JitPoisonRangeVector ranges;
    CHECK(ranges.append(JitPoisonRange(pool, first, page, true)));
    pool->addRef();
    pool->release(page, OTHER_CODE);
    SetJitCodeDecommitEnabled(true);
    ExecutableAllocator::poisonCode(cx, ranges);
    ranges.clear();
    CHECK_EQUAL(JitPageSizes(allocator).decommitted, ExecutableCodePageSize);
#ifdef DEBUG
    // Fail exactly the physical recommit. The allocation cursor, page owners,
    // code-kind bytes and caller's pool reference must remain unchanged.
    oom::SimulateOOMAfter(1, oom::THREAD_TYPE_MAIN, false);
    ExecutablePool* failedPool = nullptr;
    void* failed = allocator.alloc(page, &failedPool, OTHER_CODE);
    oom::ResetSimulatedOOM();
    CHECK(!failed && !failedPool);
    auto failedSizes = JitPageSizes(allocator);
    CHECK_EQUAL(failedSizes.decommitted, ExecutableCodePageSize);
    CHECK_EQUAL(failedSizes.other, size_t(0));
#endif
    SetJitCodeDecommitEnabled(false);
    ExecutablePool* secondPool;
    auto second = static_cast<char*>(allocator.alloc(page, &secondPool, OTHER_CODE));
    CHECK(second && secondPool == pool);
    CHECK(second == static_cast<char*>(first) + page);
    CHECK(ranges.append(JitPoisonRange(pool, second, page, true)));
    pool->addRef();
    pool->release(page, OTHER_CODE);
    ExecutableAllocator::poisonCode(cx, ranges);
    ranges.clear();
    CHECK_EQUAL(JitPageSizes(allocator).decommitted, ExecutableCodePageSize - page);
    CHECK_EQUAL(JitPageSizes(allocator).reclaimable, page);
    CHECK_EQUAL(JitPageSizes(allocator).unused, page);
    return true;
}
END_TEST(testJitCodePagesPolicyAndOOM)

BEGIN_TEST(testJitCodePagesNewCodeOOM)
{
#ifdef DEBUG
    CHECK(cx->getJitRuntime(cx));
    AutoJitCodePagePolicy policy(true);
    ExecutableAllocator allocator(cx);
    size_t page = gc::SystemPageSize();
    ExecutablePool* pool;
    auto raw = static_cast<uint8_t*>(allocator.alloc(page, &pool, OTHER_CODE));
    CHECK(raw);
    // Fail the GC-cell allocation after executable backing has succeeded.
    // JitCode::New must retire the entire header+payload allocation once.
    oom::SimulateOOMAfter(1, oom::THREAD_TYPE_MAIN, false);
    JitCode* failed = JitCode::New<NoGC>(cx, raw + sizeof(JitCode*),
        page - sizeof(JitCode*), sizeof(JitCode*), pool, OTHER_CODE);
    oom::ResetSimulatedOOM();
    CHECK(!failed);
    auto sizes = JitPageSizes(allocator);
    CHECK_EQUAL(sizes.other, size_t(0));
    CHECK_EQUAL(sizes.reclaimable, page);
    CHECK_EQUAL(sizes.unused, page);
#endif
    return true;
}
END_TEST(testJitCodePagesNewCodeOOM)

BEGIN_TEST(testJitCodePagesPoolInitOOM)
{
#ifdef DEBUG
    CHECK(cx->getJitRuntime(cx));
    AutoJitCodePagePolicy policy(true);
    bool completed = false;
    for (size_t failAt = 1; failAt < 20; failAt++) {
        ExecutableAllocator allocator(cx);
        ExecutablePool* pool = nullptr;
        oom::SimulateOOMAfter(failAt, oom::THREAD_TYPE_MAIN, true);
        void* allocation = allocator.alloc(gc::SystemPageSize(), &pool, OTHER_CODE);
        bool hitOOM = oom::HadSimulatedOOM();
        oom::ResetSimulatedOOM();
        if (allocation) {
            {
                JitRuntime::AutoPreventBackedgePatching apbp(cx);
                pool->retireCodeRange(allocation, gc::SystemPageSize());
            }
            pool->release(gc::SystemPageSize(), OTHER_CODE);
        } else {
            CHECK(!pool);
        }
        if (!hitOOM) {
            CHECK(allocation);
            completed = true;
            break;
        }
    }
    CHECK(completed);
#endif
    return true;
}
END_TEST(testJitCodePagesPoolInitOOM)
