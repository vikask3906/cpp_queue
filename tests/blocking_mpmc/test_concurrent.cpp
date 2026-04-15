// ─────────────────────────────────────────────────────────────────────────────
// MATHEMATICAL BASIS — Concurrent tests for blocking_mpmc_unbounded
//
//   No-loss / no-duplication under concurrency is a LINEARIZABILITY argument.
//   The set of all pushed items must equal the set of all popped items.
//   If we push the integers [0, N), the multiset of consumed values must be
//   exactly {0, 1, ..., N-1} — each appearing exactly once.
//
//   This catches:
//     - ABA problems (items vanish due to pointer recycling)
//     - Double-dequeue bugs (same item popped by two consumers)
//     - Lost wakeups (condition_variable fails to notify, items stay stuck)
//
//   MPMC-SPECIFIC CONCERNS:
//     1. Lock ordering: head_mutex → tail_mutex must be consistent everywhere.
//        If any path acquires them in reverse, we get deadlock.
//     2. Condition variable: notify_one must be called after tail_mutex is
//        released in push(), otherwise a spurious wakeup under head_mutex
//        could deadlock if the woken thread tries to acquire tail_mutex.
//     3. Multiple consumers: each must see a DISJOINT subset of items.
//        The union of all consumer subsets must equal the producer set.
// ─────────────────────────────────────────────────────────────────────────────
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <algorithm>
#include <atomic>
#include "blocking_mpmc_unbounded/defs.hpp"
#include "../test_helpers.hpp"

using Q = tsfqueue::__impl::blocking_mpmc_unbounded<int>;

// ── Single Producer, Single Consumer ─────────────────────────────────────────
// MATHEMATICAL BASIS: With 1P/1C, FIFO ordering must be perfectly preserved.
// The consumed sequence must be identical to the produced sequence.
TEST(BlockingMpmcConc, SPSC_FifoPreserved) {
    constexpr int N = 50000;
    Q q;

    std::thread producer([&]() {
        for (int i = 0; i < N; ++i) q.push(i);
    });

    std::vector<int> results;
    results.reserve(N);
    std::thread consumer([&]() {
        for (int i = 0; i < N; ++i) {
            int v = 0;
            q.wait_and_pop(v);
            results.push_back(v);
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(static_cast<int>(results.size()), N);
    for (int i = 0; i < N; ++i) {
        EXPECT_EQ(results[i], i) << "FIFO violated at position " << i;
    }
}

// ── Multiple Producers, Single Consumer (MPSC) ──────────────────────────────
// MATHEMATICAL BASIS: With multiple producers, per-producer FIFO is NOT
// guaranteed across producers (interleaving is non-deterministic). But the
// MULTISET of all produced items must equal the MULTISET of consumed items.
// We verify this with audit_items: sort + check for duplicates/missing.
TEST(BlockingMpmcConc, MPSC_NoLossNoDuplication) {
    constexpr int NUM_PRODUCERS = 4;
    constexpr int ITEMS_PER_PRODUCER = 10000;
    constexpr int TOTAL = NUM_PRODUCERS * ITEMS_PER_PRODUCER;
    Q q;

    std::vector<std::thread> producers;
    for (int id = 0; id < NUM_PRODUCERS; ++id) {
        producers.emplace_back([&q, id]() {
            int base = id * ITEMS_PER_PRODUCER;
            for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) q.push(base + i);
        });
    }

    std::vector<int> consumed;
    consumed.reserve(TOTAL);
    std::thread consumer([&]() {
        for (int i = 0; i < TOTAL; ++i) {
            int v = 0;
            q.wait_and_pop(v);
            consumed.push_back(v);
        }
    });

    for (auto& t : producers) t.join();
    consumer.join();

    auto r = audit_items(consumed, TOTAL);
    EXPECT_EQ(r.duplicates, 0) << "Items were popped more than once";
    EXPECT_EQ(r.missing, 0)    << "Items were lost in the queue";
}

// ── Multiple Producers, Multiple Consumers (MPMC) ────────────────────────────
// MATHEMATICAL BASIS: The full MPMC case. 4 producers × 4 consumers.
// Each consumer grabs its share. The UNION of all consumer results must
// equal {0..TOTAL-1} with zero duplicates. This is the strongest test of
// the mutex + condition_variable coordination.
//
// WHY 4×4: we want contention on both head_mutex AND tail_mutex
// simultaneously. With 1 consumer, tail_mutex contention is tested but
// head_mutex has no contention. 4×4 stresses both locks.
TEST(BlockingMpmcConc, MPMC_NoLossNoDuplication) {
    constexpr int NUM_PRODUCERS = 4;
    constexpr int NUM_CONSUMERS = 4;
    constexpr int ITEMS_PER_PRODUCER = 10000;
    constexpr int TOTAL = NUM_PRODUCERS * ITEMS_PER_PRODUCER;
    constexpr int ITEMS_PER_CONSUMER = TOTAL / NUM_CONSUMERS;
    Q q;

    // Progress counter for watchdog
    std::atomic<int> progress{0};
    Watchdog wd(progress, 5000);

    std::vector<std::thread> producers;
    for (int id = 0; id < NUM_PRODUCERS; ++id) {
        producers.emplace_back([&q, id]() {
            int base = id * ITEMS_PER_PRODUCER;
            for (int i = 0; i < ITEMS_PER_PRODUCER; ++i) q.push(base + i);
        });
    }

    std::vector<std::vector<int>> consumer_results(NUM_CONSUMERS);
    std::vector<std::thread> consumers;
    for (int id = 0; id < NUM_CONSUMERS; ++id) {
        consumers.emplace_back([&q, &consumer_results, &progress, id]() {
            for (int i = 0; i < ITEMS_PER_CONSUMER; ++i) {
                int v = 0;
                q.wait_and_pop(v);
                consumer_results[id].push_back(v);
                progress.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    // Merge all consumer results
    std::vector<int> all;
    for (auto& cr : consumer_results)
        all.insert(all.end(), cr.begin(), cr.end());

    auto r = audit_items(all, TOTAL);
    EXPECT_EQ(r.duplicates, 0) << "Double-dequeue detected in MPMC";
    EXPECT_EQ(r.missing, 0)    << "Lost items detected in MPMC";
}
