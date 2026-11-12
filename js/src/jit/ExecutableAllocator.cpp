/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * Copyright (C) 2008 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "jit/ExecutableAllocator.h"

#include "mozilla/Atomics.h"

#include "gc/Memory.h"
#include "jit/JitCompartment.h"
#include "js/MemoryMetrics.h"

using namespace js::jit;

// Embedders can override this process policy without touching worker runtimes.
// The environment switch also permits a matched standalone-shell control/arm.
static mozilla::Atomic<bool, mozilla::Relaxed> jitCodeDecommitEnabled(
    getenv("MOZ_JIT_CODE_DECOMMIT") && atoi(getenv("MOZ_JIT_CODE_DECOMMIT")));

void
js::jit::SetJitCodeDecommitEnabled(bool enabled)
{
    jitCodeDecommitEnabled = enabled;
}

bool
js::jit::JitCodeDecommitEnabled()
{
    return jitCodeDecommitEnabled;
}

size_t
ExecutablePool::pageCount() const
{
    return m_allocation.size / js::gc::SystemPageSize();
}

bool
ExecutablePool::initPageUse(bool initiallyCommitted)
{
    size_t count = pageCount();
    m_pageUse = js_pod_calloc<uint16_t>(count);
    if (!m_pageUse)
        return false;
    if (!initiallyCommitted) {
        for (size_t i = 0; i < count; i++)
            m_pageUse[i] = Decommitted;
    }
    return true;
}

bool
ExecutablePool::ensureCommitted(size_t first, size_t last)
{
    MOZ_ASSERT(m_allocator->rt_->jitRuntime()->preventBackedgePatching());
    size_t pageSize = js::gc::SystemPageSize();
    for (size_t i = first; i <= last;) {
        if (!(m_pageUse[i] & Decommitted)) {
            i++;
            continue;
        }
        size_t begin = i++;
        while (i <= last && (m_pageUse[i] & Decommitted))
            i++;
        if (!CommitExecutableMemory(m_allocation.pages + begin * pageSize,
                                    (i - begin) * pageSize, ProtectionSetting::Executable))
        {
            return false;
        }
        for (size_t page = begin; page < i; page++) {
            MOZ_ASSERT(m_pageUse[page] == Decommitted);
            m_pageUse[page] = 0;
        }
    }
    return true;
}

void
ExecutablePool::retireCodeRange(void* start, size_t size)
{
    MOZ_ASSERT(m_allocator->rt_->jitRuntime()->preventBackedgePatching());
    MOZ_ASSERT(m_pageUse);
    char* begin = static_cast<char*>(start);
    MOZ_ASSERT(size && begin >= m_allocation.pages && begin + size <= m_freePtr);
    size_t pageSize = js::gc::SystemPageSize();
    size_t first = (begin - m_allocation.pages) / pageSize;
    size_t last = (begin - m_allocation.pages + size - 1) / pageSize;
    for (size_t i = first; i <= last; i++) {
        MOZ_RELEASE_ASSERT(m_pageUse[i] && !(m_pageUse[i] & Decommitted));
        m_pageUse[i]--;
    }
}

void
ExecutablePool::decommitUnusedPages()
{
    MOZ_ASSERT(m_allocator->rt_->jitRuntime()->preventBackedgePatching());
    if (!JitCodeDecommitEnabled())
        return;

    size_t pageSize = js::gc::SystemPageSize();
    size_t count = pageCount();
    for (size_t i = 0; i < count;) {
        if (m_pageUse[i] != 0) {
            i++;
            continue;
        }
        size_t begin = i++;
        while (i < count && m_pageUse[i] == 0)
            i++;
        DecommitExecutableMemory(m_allocation.pages + begin * pageSize,
                                 (i - begin) * pageSize);
        for (size_t page = begin; page < i; page++)
            m_pageUse[page] = Decommitted;
    }
}

ExecutablePool::~ExecutablePool()
{
    MOZ_ASSERT(m_ionCodeBytes == 0);
    MOZ_ASSERT(m_baselineCodeBytes == 0);
    MOZ_ASSERT(m_regexpCodeBytes == 0);
    MOZ_ASSERT(m_otherCodeBytes == 0);

    MOZ_ASSERT(!isMarked());

    m_allocator->releasePoolPages(this);
    js_free(m_pageUse);
}

void
ExecutablePool::release(bool willDestroy)
{
    MOZ_ASSERT(m_refCount != 0);
    MOZ_ASSERT_IF(willDestroy, m_refCount == 1);
    if (--m_refCount == 0)
        js_delete(this);
}

void
ExecutablePool::release(size_t n, CodeKind kind)
{
    switch (kind) {
      case ION_CODE:
        m_ionCodeBytes -= n;
        MOZ_ASSERT(m_ionCodeBytes < m_allocation.size); // Shouldn't underflow.
        break;
      case BASELINE_CODE:
        m_baselineCodeBytes -= n;
        MOZ_ASSERT(m_baselineCodeBytes < m_allocation.size);
        break;
      case REGEXP_CODE:
        m_regexpCodeBytes -= n;
        MOZ_ASSERT(m_regexpCodeBytes < m_allocation.size);
        break;
      case OTHER_CODE:
        m_otherCodeBytes -= n;
        MOZ_ASSERT(m_otherCodeBytes < m_allocation.size);
        break;
      default:
        MOZ_CRASH("bad code kind");
    }

    release();
}

void
ExecutablePool::addRef()
{
    // It should be impossible for us to roll over, because only small
    // pools have multiple holders, and they have one holder per chunk
    // of generated code, and they only hold 16KB or so of code.
    MOZ_ASSERT(m_refCount);
    ++m_refCount;
    MOZ_ASSERT(m_refCount, "refcount overflow");
}

void*
ExecutablePool::alloc(size_t n, CodeKind kind)
{
    MOZ_ASSERT(n);
    MOZ_ASSERT(n <= available());
    size_t pageSize = js::gc::SystemPageSize();
    size_t first = (m_freePtr - m_allocation.pages) / pageSize;
    size_t last = (m_freePtr - m_allocation.pages + n - 1) / pageSize;
    if (!ensureCommitted(first, last))
        return nullptr;
    for (size_t i = first; i <= last; i++) {
        // At least a pointer-sized allocation is charged per reference, with
        // at most one straddling allocation at each boundary.
        MOZ_RELEASE_ASSERT(m_pageUse[i] < Decommitted - 1);
        m_pageUse[i]++;
    }
    void* result = m_freePtr;
    m_freePtr += n;

    switch (kind) {
      case ION_CODE:      m_ionCodeBytes      += n;        break;
      case BASELINE_CODE: m_baselineCodeBytes += n;        break;
      case REGEXP_CODE:   m_regexpCodeBytes   += n;        break;
      case OTHER_CODE:    m_otherCodeBytes    += n;        break;
      default:            MOZ_CRASH("bad code kind");
    }

    return result;
}

size_t
ExecutablePool::available() const
{
    MOZ_ASSERT(m_end >= m_freePtr);
    return m_end - m_freePtr;
}

ExecutableAllocator::ExecutableAllocator(JSRuntime* rt)
  : rt_(rt)
{
    MOZ_ASSERT(m_smallPools.empty());
}

ExecutableAllocator::~ExecutableAllocator()
{
    for (size_t i = 0; i < m_smallPools.length(); i++)
        m_smallPools[i]->release(/* willDestroy = */true);

    // If this asserts we have a pool leak.
    MOZ_ASSERT_IF(m_pools.initialized(), m_pools.empty());
}

ExecutablePool*
ExecutableAllocator::poolForSize(size_t n)
{
    // Try to fit in an existing small allocator.  Use the pool with the
    // least available space that is big enough (best-fit).  This is the
    // best strategy because (a) it maximizes the chance of the next
    // allocation fitting in a small pool, and (b) it minimizes the
    // potential waste when a small pool is next abandoned.
    ExecutablePool* minPool = nullptr;
    for (size_t i = 0; i < m_smallPools.length(); i++) {
        ExecutablePool* pool = m_smallPools[i];
        if (n <= pool->available() && (!minPool || pool->available() < minPool->available()))
            minPool = pool;
    }
    if (minPool) {
        minPool->addRef();
        return minPool;
    }

    // If the request is large, we just provide a unshared allocator
    if (n > ExecutableCodePageSize)
        return createPool(n);

    // Create a new allocator
    ExecutablePool* pool = createPool(ExecutableCodePageSize);
    if (!pool)
        return nullptr;
    // At this point, local |pool| is the owner.

    if (m_smallPools.length() < maxSmallPools) {
        // We haven't hit the maximum number of live pools; add the new pool.
        // If append() OOMs, we just return an unshared allocator.
        if (m_smallPools.append(pool))
            pool->addRef();
    } else {
        // Find the pool with the least space.
        int iMin = 0;
        for (size_t i = 1; i < m_smallPools.length(); i++) {
            if (m_smallPools[i]->available() < m_smallPools[iMin]->available())
                iMin = i;
        }

        // If the new allocator will result in more free space than the small
        // pool with the least space, then we will use it instead
        ExecutablePool* minPool = m_smallPools[iMin];
        if ((pool->available() - n) > minPool->available()) {
            minPool->release();
            m_smallPools[iMin] = pool;
            pool->addRef();
        }
    }

    // Pass ownership to the caller.
    return pool;
}

/* static */ size_t
ExecutableAllocator::roundUpAllocationSize(size_t request, size_t granularity)
{
    // Something included via windows.h defines a macro with this name,
    // which causes the function below to fail to compile.
#ifdef _MSC_VER
# undef max
#endif

    if ((std::numeric_limits<size_t>::max() - granularity) <= request)
        return OVERSIZE_ALLOCATION;

    // Round up to next page boundary
    size_t size = request + (granularity - 1);
    size = size & ~(granularity - 1);
    MOZ_ASSERT(size >= request);
    return size;
}

ExecutablePool*
ExecutableAllocator::createPool(size_t n)
{
    MOZ_ASSERT(rt_->jitRuntime()->preventBackedgePatching());

    size_t allocSize = roundUpAllocationSize(n, ExecutableCodePageSize);
    if (allocSize == OVERSIZE_ALLOCATION)
        return nullptr;

    if (!m_pools.initialized() && !m_pools.init())
        return nullptr;

    bool initiallyCommitted = !JitCodeDecommitEnabled();
    ExecutablePool::Allocation a = systemAlloc(allocSize, initiallyCommitted);
    if (!a.pages)
        return nullptr;

    ExecutablePool* pool = js_new<ExecutablePool>(this, a);
    if (!pool) {
        systemRelease(a);
        return nullptr;
    }

    if (!pool->initPageUse(initiallyCommitted)) {
        js_delete(pool);
        return nullptr;
    }

    if (!m_pools.put(pool)) {
        // Note: this will call |systemRelease(a)|.
        js_delete(pool);
        return nullptr;
    }

    return pool;
}

void*
ExecutableAllocator::alloc(size_t n, ExecutablePool** poolp, CodeKind type)
{
    // Don't race with reprotectAll called from the signal handler.
    JitRuntime::AutoPreventBackedgePatching apbp(rt_);

    // Caller must ensure 'n' is word-size aligned. If all allocations are
    // of word sized quantities, then all subsequent allocations will be
    // aligned.
    MOZ_ASSERT(roundUpAllocationSize(n, sizeof(void*)) == n);

    if (n == OVERSIZE_ALLOCATION) {
        *poolp = nullptr;
        return nullptr;
    }

    *poolp = poolForSize(n);
    if (!*poolp)
        return nullptr;

    // The reservation fits, but committing its physical pages can still fail.
    void* result = (*poolp)->alloc(n, type);
    if (!result) {
        (*poolp)->release();
        *poolp = nullptr;
    }
    return result;
}

void
ExecutableAllocator::releasePoolPages(ExecutablePool* pool)
{
    // Don't race with reprotectAll called from the signal handler.
    JitRuntime::AutoPreventBackedgePatching apbp(rt_);

    MOZ_ASSERT(pool->m_allocation.pages);
    systemRelease(pool->m_allocation);

    MOZ_ASSERT(m_pools.initialized());

    // Pool may not be present in m_pools if we hit OOM during creation.
    if (auto ptr = m_pools.lookup(pool))
        m_pools.remove(ptr);
}

void
ExecutableAllocator::purge()
{
    // Don't race with reprotectAll called from the signal handler.
    JitRuntime::AutoPreventBackedgePatching apbp(rt_);

    for (size_t i = 0; i < m_smallPools.length(); i++)
        m_smallPools[i]->release();
    m_smallPools.clear();
}

void
ExecutableAllocator::addSizeOfCode(JS::CodeSizes* sizes, mozilla::MallocSizeOf mallocSizeOf) const
{
    sizes->poolMetadata += m_smallPools.sizeOfExcludingThis(mallocSizeOf);
    if (m_pools.initialized()) {
        sizes->poolMetadata += m_pools.sizeOfExcludingThis(mallocSizeOf);
        for (ExecPoolHashSet::Range r = m_pools.all(); !r.empty(); r.popFront()) {
            ExecutablePool* pool = r.front();
            size_t decommitted = 0;
            size_t pageSize = js::gc::SystemPageSize();
            for (size_t i = 0; i < pool->pageCount(); i++) {
                if (pool->m_pageUse[i] & ExecutablePool::Decommitted)
                    decommitted += pageSize;
                else if (pool->m_pageUse[i] == 0)
                    sizes->reclaimable += pageSize;
            }
            sizes->pools++;
            sizes->reserved += pool->m_allocation.size;
            sizes->decommitted += decommitted;
            sizes->poolMetadata += mallocSizeOf(pool) + mallocSizeOf(pool->m_pageUse);
            sizes->ion      += pool->m_ionCodeBytes;
            sizes->baseline += pool->m_baselineCodeBytes;
            sizes->regexp   += pool->m_regexpCodeBytes;
            sizes->other    += pool->m_otherCodeBytes;
            sizes->unused   += pool->m_allocation.size - decommitted - pool->m_ionCodeBytes
                                                       - pool->m_baselineCodeBytes
                                                       - pool->m_regexpCodeBytes
                                                       - pool->m_otherCodeBytes;
        }
    }
}

void
ExecutableAllocator::reprotectAll(ProtectionSetting protection)
{
    if (!m_pools.initialized())
        return;

    for (ExecPoolHashSet::Range r = m_pools.all(); !r.empty(); r.popFront())
        reprotectPool(rt_, r.front(), protection);
}

/* static */ void
ExecutableAllocator::reprotectPool(JSRuntime* rt, ExecutablePool* pool, ProtectionSetting protection)
{
    // Don't race with reprotectAll called from the signal handler.
    MOZ_ASSERT(rt->jitRuntime()->preventBackedgePatching() || rt->handlingJitInterrupt());

    size_t pageSize = js::gc::SystemPageSize();
    size_t count = (pool->m_freePtr - pool->m_allocation.pages + pageSize - 1) / pageSize;
    for (size_t i = 0; i < count;) {
        if (pool->m_pageUse[i] & ExecutablePool::Decommitted) {
            i++;
            continue;
        }
        size_t begin = i++;
        while (i < count && !(pool->m_pageUse[i] & ExecutablePool::Decommitted))
            i++;
        if (!ReprotectRegion(pool->m_allocation.pages + begin * pageSize,
                             (i - begin) * pageSize, protection))
        {
            MOZ_CRASH();
        }
    }
}

/* static */ void
ExecutableAllocator::poisonCode(JSRuntime* rt, JitPoisonRangeVector& ranges)
{
    MOZ_ASSERT(CurrentThreadCanAccessRuntime(rt));

    // Don't race with reprotectAll called from the signal handler.
    JitRuntime::AutoPreventBackedgePatching apbp(rt);

#ifdef DEBUG
    // Make sure no pools have the mark bit set.
    for (size_t i = 0; i < ranges.length(); i++)
        MOZ_ASSERT(!ranges[i].pool->isMarked());
#endif

    for (size_t i = 0; i < ranges.length(); i++) {
        ExecutablePool* pool = ranges[i].pool;
        if (pool->m_refCount == 1) {
            // This is the last reference so the release() call below will
            // unmap the memory. Don't bother poisoning it.
            continue;
        }

        MOZ_ASSERT(pool->m_refCount > 1);

        // Use the pool's mark bit to indicate we made the pool writable.
        // This avoids reprotecting a pool multiple times.
        if (!pool->isMarked()) {
            reprotectPool(rt, pool, ProtectionSetting::Writable);
            pool->mark();
        }

        memset(ranges[i].start, JS_SWEPT_CODE_PATTERN, ranges[i].size);
    }

    // All poison writes must finish before any page can lose backing. A range
    // keeps page ownership even if its JitCode's pool reference was released.
    for (size_t i = 0; i < ranges.length(); i++) {
        if (ranges[i].retirePages)
            ranges[i].pool->retireCodeRange(ranges[i].start, ranges[i].size);
    }

    // Make the pools executable again, reclaim empty pages, then drop refs.
    for (size_t i = 0; i < ranges.length(); i++) {
        ExecutablePool* pool = ranges[i].pool;
        if (pool->isMarked()) {
            reprotectPool(rt, pool, ProtectionSetting::Executable);
            pool->decommitUnusedPages();
            pool->unmark();
        }
        pool->release();
    }
}

ExecutablePool::Allocation
ExecutableAllocator::systemAlloc(size_t n, bool initiallyCommitted)
{
    void* allocation = AllocateExecutableMemory(n, ProtectionSetting::Executable,
                                                initiallyCommitted);
    ExecutablePool::Allocation alloc = { reinterpret_cast<char*>(allocation), n };
    return alloc;
}

void
ExecutableAllocator::systemRelease(const ExecutablePool::Allocation& alloc)
{
    DeallocateExecutableMemory(alloc.pages, alloc.size);
}
