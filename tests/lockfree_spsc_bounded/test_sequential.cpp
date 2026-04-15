// ─────────────────────────────────────────────────────────────────────────────
// MATHEMATICAL BASIS — Sequential tests for lockfree_spsc_bounded
//
//   The ring buffer uses two indices: head (consumer) and tail (producer).
//   The slot index is computed as (idx % buffer_size) where buffer_size =
//   Capacity + 1 (one wasted slot distinguishes full from empty).
//
//   KEY INVARIANTS TO TEST:
//     1. FIFO ordering: push(a) before push(b) ⇒ pop returns a before b
//     2. Capacity enforcement: exactly Capacity items can be stored; the
//        (Capacity+1)-th push must fail
//     3. Modular arithmetic: (tail + 1) % buffer_size correctly wraps
//
//   WHY SEQUENTIAL FIRST:
//     The ring buffer arithmetic (modulo, wasted slot) must be correct
//     independent of atomics. If (tail+1) % buffer_size overflows or the
//     wasted-slot math is wrong, adding threads only makes the bug
//     non-deterministic. Sequential tests isolate the arithmetic.
// ─────────────────────────────────────────────────────────────────────────────
#include <gtest/gtest.h>
#include <string>
#include "lockfree_spsc_bounded/defs.hpp"

TEST(SpscBoundedSeq, EmptyOnConstruction) {
    tsfqueue::__impl::lockfree_spsc_bounded<int, 8> q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
}

TEST(SpscBoundedSeq, PushThenPop) {
    tsfqueue::__impl::lockfree_spsc_bounded<int, 8> q;
    EXPECT_TRUE(q.try_push(42));
    int v = 0;
    EXPECT_TRUE(q.try_pop(v));
    EXPECT_EQ(v, 42);
}

// MATHEMATICAL BASIS: The queue must hold exactly Capacity items.
// buffer_size = Capacity+1, and the full condition is:
//   (tail + 1) % buffer_size == head
// So with Capacity=4, buffer_size=5, after 4 pushes:
//   tail=4, next_tail=(4+1)%5=0==head → full.
TEST(SpscBoundedSeq, CapacityEnforcement) {
    tsfqueue::__impl::lockfree_spsc_bounded<int, 4> q;
    EXPECT_TRUE(q.try_push(1));
    EXPECT_TRUE(q.try_push(2));
    EXPECT_TRUE(q.try_push(3));
    EXPECT_TRUE(q.try_push(4));
    EXPECT_FALSE(q.try_push(5)) << "Queue accepted more than Capacity items";
    EXPECT_EQ(q.size(), 4u);
}

// MATHEMATICAL BASIS: After draining one slot from a full queue, exactly
// one more push must succeed. This tests the boundary between full and
// not-full, which relies on head advancing and the head_cache refresh.
TEST(SpscBoundedSeq, PushAfterPartialDrain) {
    tsfqueue::__impl::lockfree_spsc_bounded<int, 4> q;
    for (int i = 0; i < 4; ++i) q.try_push(i);

    int v = 0;
    q.try_pop(v);                      // free exactly 1 slot
    EXPECT_TRUE(q.try_push(99));       // must succeed
    EXPECT_FALSE(q.try_push(100));     // must fail again
}

TEST(SpscBoundedSeq, FifoOrdering) {
    tsfqueue::__impl::lockfree_spsc_bounded<int, 8> q;
    for (int i = 0; i < 8; ++i) q.try_push(i * 10);
    for (int i = 0; i < 8; ++i) {
        int v = -1;
        EXPECT_TRUE(q.try_pop(v));
        EXPECT_EQ(v, i * 10) << "FIFO broken at position " << i;
    }
}

TEST(SpscBoundedSeq, TryPopOnEmptyReturnsFalse) {
    tsfqueue::__impl::lockfree_spsc_bounded<int, 8> q;
    int v = -1;
    EXPECT_FALSE(q.try_pop(v));
}

TEST(SpscBoundedSeq, PeekDoesNotRemove) {
    tsfqueue::__impl::lockfree_spsc_bounded<int, 8> q;
    int v = 0;
    EXPECT_FALSE(q.peek(v));  // empty

    q.try_push(42);
    q.try_push(99);

    EXPECT_TRUE(q.peek(v));
    EXPECT_EQ(v, 42);
    EXPECT_TRUE(q.peek(v));    // still 42
    EXPECT_EQ(v, 42);
    EXPECT_EQ(q.size(), 2u);  // nothing removed
}

// MATHEMATICAL BASIS: try_emplace must construct T in-place using
// forwarded constructor arguments. For std::string(5, 'A'), this produces
// "AAAAA" without creating an intermediate string object.
TEST(SpscBoundedSeq, EmplaceConstructsInPlace) {
    tsfqueue::__impl::lockfree_spsc_bounded<std::string, 8> q;
    EXPECT_TRUE(q.try_emplace("hello"));
    EXPECT_TRUE(q.try_emplace("world"));

    std::string v;
    EXPECT_TRUE(q.try_pop(v));
    EXPECT_EQ(v, "hello");
    EXPECT_TRUE(q.try_pop(v));
    EXPECT_EQ(v, "world");
}

TEST(SpscBoundedSeq, SizeTracksOperations) {
    tsfqueue::__impl::lockfree_spsc_bounded<int, 8> q;
    EXPECT_EQ(q.size(), 0u);
    q.try_push(1); EXPECT_EQ(q.size(), 1u);
    q.try_push(2); EXPECT_EQ(q.size(), 2u);
    int v;
    q.try_pop(v);  EXPECT_EQ(q.size(), 1u);
    q.try_pop(v);  EXPECT_EQ(q.size(), 0u);
}
