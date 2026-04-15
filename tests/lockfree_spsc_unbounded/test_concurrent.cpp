// ─────────────────────────────────────────────────────────────────────────────
// MATHEMATICAL BASIS — Concurrent tests for lockfree_spsc_unbounded
//
//   The unbounded SPSC queue uses atomic next pointers for synchronization:
//     - Producer: tail->next.store(new_stub, memory_order_release)
//     - Consumer: head->next.load(memory_order_acquire)
//
//   The release/acquire pair forms a "happens-before" edge:
//     Producer writes tail->data BEFORE the release store.
//     Consumer reads head->data AFTER the acquire load.
//     Therefore, consumer always sees fully written data.
//
//   WHAT CAN GO WRONG:
//     1. If release/acquire is replaced with relaxed, the CPU may reorder
//        the data write after the next pointer write. The consumer then
//        reads unitialized data from head->data.
//     2. If the stub node pattern is wrong (e.g., reading data from
//        next_node instead of head), the consumer reads the wrong slot.
//     3. Memory leak: if nodes aren't freed after pop, 100K pushes
//        leak 100K × sizeof(node) bytes.
//
//   UNBOUNDED-SPECIFIC NOTE:
//     Unlike the bounded queue, every push does `new node()`. This means
//     the allocator itself is part of the concurrent execution path.
//     Under high contention, the allocator's internal locks may serialize
//     the producer, but this must NOT affect correctness.
// ─────────────────────────────────────────────────────────────────────────────
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "lockfree_spsc_unbounded/defs.hpp"
#include "../test_helpers.hpp"

using Q = tsfqueue::__impl::lockfree_spsc_unbounded<int>;

// ── Core correctness: strict FIFO under concurrency ──────────────────────────
// MATHEMATICAL BASIS: With 1P/1C, the consumed sequence must be identical
// to [0, N). Any out-of-order element proves a memory ordering bug.
TEST(SpscUnboundedConc, StrictFIFO_100K) {
    constexpr int N = 100000;
    Q q;

    std::thread producer([&]() {
        for (int i = 0; i < N; ++i) q.push(i);
    });

    std::vector<int> consumed;
    consumed.reserve(N);
    std::thread consumer([&]() {
        for (int i = 0; i < N; ++i) {
            int v = 0;
            q.wait_and_pop(v);
            consumed.push_back(v);
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(static_cast<int>(consumed.size()), N);
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(consumed[i], i) << "FIFO violated at position " << i;
    }
    EXPECT_TRUE(q.empty());
}

// ── Multiset audit ───────────────────────────────────────────────────────────
// Uses the formal audit_items helper for a second verification pass.
TEST(SpscUnboundedConc, AuditNoLossNoDuplication) {
    constexpr int N = 100000;
    Q q;

    std::thread producer([&]() {
        for (int i = 0; i < N; ++i) q.push(i);
    });

    std::vector<int> consumed;
    consumed.reserve(N);
    std::thread consumer([&]() {
        for (int i = 0; i < N; ++i) {
            int v = 0;
            q.wait_and_pop(v);
            consumed.push_back(v);
        }
    });

    producer.join();
    consumer.join();

    auto r = audit_items(consumed, N);
    EXPECT_EQ(r.duplicates, 0) << "Double-dequeue from linked list";
    EXPECT_EQ(r.missing, 0)    << "Lost items — possible dangling pointer";
}

// ── Slow consumer stress test ────────────────────────────────────────────────
// MATHEMATICAL BASIS: The unbounded queue must grow without bound when the
// producer is faster than the consumer. If the linked list management is
// wrong, the queue will either crash (dangling pointer) or lose items.
// We simulate this by making the producer fire instantly and the consumer
// drain slowly.
TEST(SpscUnboundedConc, SlowConsumer) {
    constexpr int N = 50000;
    Q q;

    // Producer: push everything as fast as possible
    std::thread producer([&]() {
        for (int i = 0; i < N; ++i) q.push(i);
    });

    producer.join();  // Producer finishes before consumer even starts

    // Consumer: drain the entire backlog
    std::vector<int> consumed;
    consumed.reserve(N);
    for (int i = 0; i < N; ++i) {
        int v = 0;
        EXPECT_TRUE(q.try_pop(v));
        consumed.push_back(v);
    }

    ASSERT_EQ(static_cast<int>(consumed.size()), N);
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(consumed[i], i);
    }
}
