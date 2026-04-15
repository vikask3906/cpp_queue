// ─────────────────────────────────────────────────────────────────────────────
// MATHEMATICAL BASIS — Sequential tests for lockfree_spsc_unbounded
//
//   The unbounded queue is a singly-linked list with a STUB NODE pattern:
//     - head and tail always point to valid nodes (never nullptr)
//     - tail points to the "stub" — a node with next==nullptr and no data
//     - push writes data into the stub, creates a new stub, links them
//     - pop reads data from head, deletes it, advances head
//
//   KEY INVARIANTS:
//     1. Empty ⟺ head->next == nullptr (head IS the stub)
//     2. After push: head->next != nullptr (the stub has been filled)
//     3. After pop of last element: head->next == nullptr again
//     4. Memory: every push allocates exactly 1 node, every pop frees
//        exactly 1 node. If this is wrong, we leak or double-free.
//
//   WHY SEQUENTIAL:
//     The stub node pattern is subtle. If data is read from the wrong node
//     (e.g., from next_node instead of head) or if the stub isn't properly
//     recycled, FIFO breaks. Sequential tests catch this before atomics
//     add noise.
// ─────────────────────────────────────────────────────────────────────────────
#include <gtest/gtest.h>
#include <string>
#include "lockfree_spsc_unbounded/defs.hpp"

using Q = tsfqueue::__impl::lockfree_spsc_unbounded<int>;

TEST(SpscUnboundedSeq, EmptyOnConstruction) {
    Q q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
}

TEST(SpscUnboundedSeq, PushThenPop) {
    Q q;
    q.push(42);
    int v = 0;
    EXPECT_TRUE(q.try_pop(v));
    EXPECT_EQ(v, 42);
    EXPECT_TRUE(q.empty());
}

TEST(SpscUnboundedSeq, FifoOrdering_10Elements) {
    Q q;
    for (int i = 0; i < 10; ++i) q.push(i);
    EXPECT_EQ(q.size(), 10u);

    for (int i = 0; i < 10; ++i) {
        int v = -1;
        EXPECT_TRUE(q.try_pop(v));
        EXPECT_EQ(v, i) << "FIFO broken at position " << i;
    }
    EXPECT_TRUE(q.empty());
}

TEST(SpscUnboundedSeq, TryPopOnEmptyReturnsFalse) {
    Q q;
    int v = -1;
    EXPECT_FALSE(q.try_pop(v));
}

// MATHEMATICAL BASIS: Peek returns a COPY of head->data without advancing
// the head pointer. Calling peek twice must return the same value, and
// size must remain unchanged.
TEST(SpscUnboundedSeq, PeekDoesNotRemove) {
    Q q;
    int v = 0;
    EXPECT_FALSE(q.peek(v));  // empty

    q.push(42);
    q.push(99);

    EXPECT_TRUE(q.peek(v));
    EXPECT_EQ(v, 42);
    EXPECT_TRUE(q.peek(v));   // still 42
    EXPECT_EQ(v, 42);
    EXPECT_EQ(q.size(), 2u);
}

// MATHEMATICAL BASIS: emplace must construct T in-place from forwarded
// arguments. The resulting value in the queue must be identical to what
// T's constructor would produce.
TEST(SpscUnboundedSeq, EmplaceConstructsInPlace) {
    tsfqueue::__impl::lockfree_spsc_unbounded<std::string> q;
    q.emplace("hello");
    q.emplace("world");

    std::string v;
    EXPECT_TRUE(q.try_pop(v));
    EXPECT_EQ(v, "hello");
    EXPECT_TRUE(q.try_pop(v));
    EXPECT_EQ(v, "world");
}

// MATHEMATICAL BASIS: Burst N items, then drain all N. This tests the
// linked list growth to N nodes followed by complete deallocation back
// down to the single stub. If any node is leaked or the stub is incorrectly
// advanced, the destructor will either double-free or leak.
TEST(SpscUnboundedSeq, BurstAndDrain_10K) {
    Q q;
    constexpr int N = 10000;
    for (int i = 0; i < N; ++i) q.push(i);
    EXPECT_EQ(q.size(), static_cast<size_t>(N));

    for (int i = 0; i < N; ++i) {
        int v = -1;
        EXPECT_TRUE(q.try_pop(v));
        EXPECT_EQ(v, i);
    }
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
}

TEST(SpscUnboundedSeq, SizeTracksOperations) {
    Q q;
    EXPECT_EQ(q.size(), 0u);
    q.push(1); EXPECT_EQ(q.size(), 1u);
    q.push(2); EXPECT_EQ(q.size(), 2u);
    int v;
    q.try_pop(v); EXPECT_EQ(q.size(), 1u);
    q.try_pop(v); EXPECT_EQ(q.size(), 0u);
}
