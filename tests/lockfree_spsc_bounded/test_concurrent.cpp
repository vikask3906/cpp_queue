// ─────────────────────────────────────────────────────────────────────────────
// MATHEMATICAL BASIS — Concurrent tests for lockfree_spsc_bounded
//
//   The SPSC bounded queue uses a ring buffer with two atomic indices:
//     - tail (written only by producer, read by consumer via tail_cache)
//     - head (written only by consumer, read by producer via head_cache)
//
//   WHAT CAN GO WRONG:
//     1. Memory ordering: if the producer's store to arr[tail] is reordered
//        AFTER the store to tail (the publication), the consumer reads stale
//        data from the array slot. memory_order_release on tail.store and
//        memory_order_acquire on tail.load prevent this.
//     2. Cache staleness: head_cache/tail_cache are non-atomic local copies.
//        If the refresh logic is wrong, the producer sees "full" when it
//        isn't (stale head_cache) or the consumer sees "empty" when it isn't
//        (stale tail_cache). This causes items to be silently dropped.
//     3. Wrap-around: after Capacity pushes, tail wraps from buffer_size-1
//        back to 0. If modular arithmetic is wrong, the producer overwrites
//        unconsumed slots → data corruption.
//
//   THE CONTRACT:
//     This queue is ONLY safe with exactly 1 producer and 1 consumer.
//     Having 2 producers is UNDEFINED BEHAVIOUR. Tests enforce this.
// ─────────────────────────────────────────────────────────────────────────────
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include "lockfree_spsc_bounded/defs.hpp"
#include "../test_helpers.hpp"

// ── Core correctness: all items survive the round trip ───────────────────────
// MATHEMATICAL BASIS: Push integers [0, N). Pop all N. The consumed multiset
// must equal {0..N-1}. With SPSC, FIFO order is additionally guaranteed,
// so we check strict sequential equality, not just multiset equality.
TEST(SpscBoundedConc, NoLossNoDuplication_200K) {
    constexpr int TOTAL = 200000;
    tsfqueue::__impl::lockfree_spsc_bounded<int, 1024> q;

    std::thread producer([&]() {
        for (int i = 0; i < TOTAL; ++i) q.wait_and_push(i);
    });

    std::vector<int> consumed;
    consumed.reserve(TOTAL);
    std::thread consumer([&]() {
        for (int i = 0; i < TOTAL; ++i) {
            int v = 0;
            q.wait_and_pop(v);
            consumed.push_back(v);
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(static_cast<int>(consumed.size()), TOTAL);
    // SPSC guarantees strict FIFO, so check order, not just multiset
    for (int i = 0; i < TOTAL; ++i) {
        EXPECT_EQ(consumed[i], i) << "FIFO violated or data corrupted at " << i;
    }
}

// ── Wrap-around test ─────────────────────────────────────────────────────────
// MATHEMATICAL BASIS: With Capacity=8, buffer_size=9. After 8 pushes,
// tail wraps from index 8 to index 0 (via (8+1)%9 = 0). We force 1000
// full laps (8000 total operations) to hammer the wrap-around path.
//
// This catches off-by-one errors in modular arithmetic: if buffer_size
// were used as Capacity (missing the +1), the full/empty distinction
// breaks on wrap.
TEST(SpscBoundedConc, WrapAroundMultipleLaps) {
    constexpr int CAP = 8;
    constexpr int LAPS = 1000;
    tsfqueue::__impl::lockfree_spsc_bounded<int, CAP> q;

    std::atomic<bool> ok{true};

    std::thread producer([&]() {
        for (int lap = 0; lap < LAPS; ++lap)
            for (int i = 0; i < CAP; ++i)
                q.wait_and_push(lap * CAP + i);
    });

    std::thread consumer([&]() {
        for (int lap = 0; lap < LAPS; ++lap) {
            for (int i = 0; i < CAP; ++i) {
                int v = -1;
                q.wait_and_pop(v);
                if (v != lap * CAP + i) ok.store(false);
            }
        }
    });

    producer.join();
    consumer.join();
    EXPECT_TRUE(ok.load()) << "Wrap-around corrupted item values";
}

// ── Back-pressure test ───────────────────────────────────────────────────────
// MATHEMATICAL BASIS: A bounded queue must NEVER silently drop items.
// When the queue is full, try_push must return false (not overwrite).
// This is the back-pressure guarantee.
TEST(SpscBoundedConc, BackPressurePreventsSilentLoss) {
    constexpr int CAP = 4;
    tsfqueue::__impl::lockfree_spsc_bounded<int, CAP> q;

    // Fill the queue completely
    for (int i = 0; i < CAP; ++i) ASSERT_TRUE(q.try_push(i));

    // Must reject — queue is full
    EXPECT_FALSE(q.try_push(999));

    // Drain one slot
    int v = -1;
    ASSERT_TRUE(q.try_pop(v));
    EXPECT_EQ(v, 0);

    // Now exactly one slot is free
    EXPECT_TRUE(q.try_push(999));
    EXPECT_FALSE(q.try_push(1000)); // full again
}

// ── Audit with multiset verification ─────────────────────────────────────────
// Uses the audit_items helper to formally verify no-loss/no-duplication.
TEST(SpscBoundedConc, AuditMultiset) {
    constexpr int TOTAL = 100000;
    tsfqueue::__impl::lockfree_spsc_bounded<int, 512> q;

    std::thread producer([&]() {
        for (int i = 0; i < TOTAL; ++i) q.wait_and_push(i);
    });

    std::vector<int> consumed;
    consumed.reserve(TOTAL);
    std::thread consumer([&]() {
        for (int i = 0; i < TOTAL; ++i) {
            int v = 0;
            q.wait_and_pop(v);
            consumed.push_back(v);
        }
    });

    producer.join();
    consumer.join();

    auto r = audit_items(consumed, TOTAL);
    EXPECT_EQ(r.duplicates, 0) << "Double-read from ring buffer";
    EXPECT_EQ(r.missing, 0)    << "Lost items — possible overwrite or cache bug";
}
