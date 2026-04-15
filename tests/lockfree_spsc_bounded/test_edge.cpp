// ─────────────────────────────────────────────────────────────────────────────
// MATHEMATICAL BASIS — Edge case tests for lockfree_spsc_bounded
//
//   RING BUFFER EDGE CASES:
//     1. Capacity=1: buffer_size=2. Only one usable slot. The queue
//        alternates between full and empty on every single operation.
//        This is the tightest possible exercise of the modulo arithmetic.
//     2. Empty pop: try_pop on empty must return false without corruption.
//        The tail_cache must correctly report "no new items".
//     3. Alternating push/pop: exercises the case where head and tail
//        chase each other around the ring. If the cached indices go stale
//        and are never refreshed, this pattern causes false "full" or
//        false "empty" reports.
// ─────────────────────────────────────────────────────────────────────────────
#include <gtest/gtest.h>
#include "lockfree_spsc_bounded/defs.hpp"

// MATHEMATICAL BASIS: With Capacity=1, buffer_size=2. The only valid states
// are head==tail (empty) and (tail+1)%2==head (full). There is zero room
// for error in the modulo math.
TEST(SpscBoundedEdge, Capacity1_SingleSlot) {
    tsfqueue::__impl::lockfree_spsc_bounded<int, 1> q;

    EXPECT_TRUE(q.empty());
    EXPECT_TRUE(q.try_push(42));
    EXPECT_FALSE(q.try_push(99));   // full: only 1 slot
    EXPECT_EQ(q.size(), 1u);

    int v = -1;
    EXPECT_TRUE(q.try_pop(v));
    EXPECT_EQ(v, 42);
    EXPECT_TRUE(q.empty());

    // Repeat: verify it works after a full cycle
    EXPECT_TRUE(q.try_push(100));
    EXPECT_TRUE(q.try_pop(v));
    EXPECT_EQ(v, 100);
}

// MATHEMATICAL BASIS: Alternating push/pop forces head and tail to advance
// in lockstep. After N iterations, both have wrapped around the ring
// N/buffer_size times. If the cache refresh (head_cache or tail_cache) is
// wrong, at some point the queue falsely reports full or empty.
TEST(SpscBoundedEdge, AlternatingPushPop_1000) {
    tsfqueue::__impl::lockfree_spsc_bounded<int, 4> q;
    for (int i = 0; i < 1000; ++i) {
        EXPECT_TRUE(q.try_push(i)) << "False full at iteration " << i;
        int v = -1;
        EXPECT_TRUE(q.try_pop(v))  << "False empty at iteration " << i;
        EXPECT_EQ(v, i);
    }
}

// MATHEMATICAL BASIS: Multiple pops on empty must all return false and
// must not corrupt the internal state. After the failed pops, a fresh
// push+pop must still work correctly.
TEST(SpscBoundedEdge, RepeatedEmptyPop) {
    tsfqueue::__impl::lockfree_spsc_bounded<int, 4> q;
    int v = -1;

    for (int i = 0; i < 100; ++i) {
        EXPECT_FALSE(q.try_pop(v));
    }

    // Queue must still be functional after 100 failed pops
    EXPECT_TRUE(q.try_push(42));
    EXPECT_TRUE(q.try_pop(v));
    EXPECT_EQ(v, 42);
}

// MATHEMATICAL BASIS: Fill, drain, fill, drain. Two complete cycles exercise
// the transition from full→empty→full→empty. The second fill starts with
// head and tail at non-zero positions, so the modular wrap is tested from
// a different starting offset than the first fill.
TEST(SpscBoundedEdge, FillDrainTwice) {
    tsfqueue::__impl::lockfree_spsc_bounded<int, 4> q;

    // First cycle
    for (int i = 0; i < 4; ++i) EXPECT_TRUE(q.try_push(i));
    EXPECT_FALSE(q.try_push(999));
    for (int i = 0; i < 4; ++i) {
        int v = -1;
        EXPECT_TRUE(q.try_pop(v));
        EXPECT_EQ(v, i);
    }
    EXPECT_TRUE(q.empty());

    // Second cycle — indices start at 4, not 0
    for (int i = 10; i < 14; ++i) EXPECT_TRUE(q.try_push(i));
    EXPECT_FALSE(q.try_push(999));
    for (int i = 10; i < 14; ++i) {
        int v = -1;
        EXPECT_TRUE(q.try_pop(v));
        EXPECT_EQ(v, i);
    }
    EXPECT_TRUE(q.empty());
}
