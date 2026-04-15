// ─────────────────────────────────────────────────────────────────────────────
// MATHEMATICAL BASIS — Sequential tests for blocking_mpmc_unbounded
//
//   These verify the queue is a strict FIFO (total order on single-thread ops).
//   For a single thread operating serially, the queue must behave identically
//   to std::queue. Any deviation here means the core data structure (linked
//   list node management, head/tail pointers, stub node pattern) is broken
//   before we even consider concurrency.
//
//   PROPERTY UNDER TEST:
//     If item a was enqueued before item b by the same thread, then a must
//     be dequeued before b.  This is the FIFO total order invariant.
//
//   WHY TEST THIS SEPARATELY FROM CONCURRENCY:
//     A concurrent bug can mask a sequential bug. If push/pop logic itself
//     is wrong, adding threads only makes the failure non-deterministic and
//     harder to diagnose. Sequential tests isolate the data structure logic.
// ─────────────────────────────────────────────────────────────────────────────
#include <gtest/gtest.h>
#include <string>
#include <memory>
#include "blocking_mpmc_unbounded/defs.hpp"

using Q = tsfqueue::__impl::blocking_mpmc_unbounded<int>;

TEST(BlockingMpmcSeq, EmptyOnConstruction) {
    Q q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
}

TEST(BlockingMpmcSeq, NotEmptyAfterPush) {
    Q q;
    q.push(1);
    EXPECT_FALSE(q.empty());
    EXPECT_EQ(q.size(), 1u);
}

TEST(BlockingMpmcSeq, PushThenPop) {
    Q q;
    q.push(42);
    int v = 0;
    EXPECT_TRUE(q.try_pop(v));
    EXPECT_EQ(v, 42);
}

TEST(BlockingMpmcSeq, EmptyAfterDrain) {
    Q q;
    q.push(1); q.push(2); q.push(3);
    int v;
    q.try_pop(v); q.try_pop(v); q.try_pop(v);
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
}

// MATHEMATICAL BASIS: FIFO is a total order ≡ for same-thread pushes,
// pop order must be identical to push order. N=10 covers multiple node
// allocations in the linked list.
TEST(BlockingMpmcSeq, FifoOrdering_10Elements) {
    Q q;
    for (int i = 0; i < 10; ++i) q.push(i);
    for (int i = 0; i < 10; ++i) {
        int v = -1;
        EXPECT_TRUE(q.try_pop(v));
        EXPECT_EQ(v, i) << "FIFO broken at position " << i;
    }
}

TEST(BlockingMpmcSeq, TryPopOnEmptyReturnsFalse) {
    Q q;
    int v = -1;
    EXPECT_FALSE(q.try_pop(v));
}

// MATHEMATICAL BASIS: The shared_ptr overload of try_pop returns nullptr
// when empty. This verifies the queue distinguishes "no element" from
// "element with value 0" — a property that raw bool + ref cannot test.
TEST(BlockingMpmcSeq, SharedPtrPopEmpty) {
    Q q;
    auto ptr = q.try_pop();
    EXPECT_EQ(ptr, nullptr);
}

TEST(BlockingMpmcSeq, SharedPtrPopNonEmpty) {
    Q q;
    q.push(99);
    auto ptr = q.try_pop();
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(*ptr, 99);
}

TEST(BlockingMpmcSeq, SizeTracksOperations) {
    Q q;
    EXPECT_EQ(q.size(), 0u);
    q.push(1); EXPECT_EQ(q.size(), 1u);
    q.push(2); EXPECT_EQ(q.size(), 2u);
    int v;
    q.try_pop(v); EXPECT_EQ(q.size(), 1u);
    q.try_pop(v); EXPECT_EQ(q.size(), 0u);
}

// MATHEMATICAL BASIS: The queue must work with non-trivial types.
// std::string exercises the move constructor path through the shared_ptr
// indirection and ensures no use-after-move corruption occurs.
TEST(BlockingMpmcSeq, StringPayload) {
    tsfqueue::__impl::blocking_mpmc_unbounded<std::string> q;
    q.push("hello"); q.push("world");
    std::string v;
    q.try_pop(v); EXPECT_EQ(v, "hello");
    q.try_pop(v); EXPECT_EQ(v, "world");
}
