// ─────────────────────────────────────────────────────────────────────────────
// MATHEMATICAL BASIS — Edge case tests for blocking_mpmc_unbounded
//
//   Edge cases exercise boundary conditions that are algebraically special:
//     - Empty queue: head == tail (pointing to the same stub node)
//     - Single element: head->next is the only data node
//     - Interleaved push/pop: the stub node is constantly recycled
//
//   BLOCKING-SPECIFIC EDGES:
//     1. wait_and_pop must block when empty and unblock when push arrives.
//        This tests the condition_variable wakeup path.
//     2. Multiple waiters: if N threads are blocked on wait_and_pop, pushing
//        N items must wake exactly N threads (no lost wakeups).
//     3. try_pop must never block, regardless of queue state.
// ─────────────────────────────────────────────────────────────────────────────
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include "blocking_mpmc_unbounded/defs.hpp"

using Q = tsfqueue::__impl::blocking_mpmc_unbounded<int>;

// MATHEMATICAL BASIS: wait_and_pop must block indefinitely on an empty queue
// until push provides data. This verifies the condition_variable wakeup
// is correctly paired with push's notify_one.
TEST(BlockingMpmcEdge, WaitAndPopBlocksUntilPush) {
    Q q;

    std::thread producer([&]() {
        // Delay to ensure consumer is already waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        q.push(42);
    });

    int v = 0;
    q.wait_and_pop(v);  // Must block here until producer pushes
    EXPECT_EQ(v, 42);

    producer.join();
}

// MATHEMATICAL BASIS: With N threads waiting in wait_and_pop, pushing exactly
// N items must wake all N threads. If any thread remains blocked, we have a
// "lost wakeup" — notify_one failed to propagate.
TEST(BlockingMpmcEdge, MultipleWaitersAllWoken) {
    constexpr int N = 4;
    Q q;

    std::atomic<int> woken{0};
    std::vector<std::thread> waiters;
    for (int i = 0; i < N; ++i) {
        waiters.emplace_back([&q, &woken]() {
            int v = 0;
            q.wait_and_pop(v);
            woken.fetch_add(1, std::memory_order_relaxed);
        });
    }

    // Give waiters time to block on the condition variable
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Push exactly N items — each must wake exactly one waiter
    for (int i = 0; i < N; ++i) q.push(i);

    for (auto& t : waiters) t.join();
    EXPECT_EQ(woken.load(), N) << "Lost wakeup: not all waiters were notified";
}

// MATHEMATICAL BASIS: Alternating push-pop exercises the stub node recycling
// path. After each pop, head advances and the old node is freed. A fresh
// push must correctly chain a new node from the current stub.
// If the stub management is wrong, this pattern corrupts the list.
TEST(BlockingMpmcEdge, AlternatingPushPop) {
    Q q;
    for (int i = 0; i < 1000; ++i) {
        q.push(i);
        int v = -1;
        EXPECT_TRUE(q.try_pop(v));
        EXPECT_EQ(v, i);
        EXPECT_TRUE(q.empty());
    }
}

// MATHEMATICAL BASIS: The shared_ptr pop overload must return nullptr on
// empty and a valid pointer otherwise. This tests the try_get → nullptr
// propagation path independently from the reference-based try_pop.
TEST(BlockingMpmcEdge, SharedPtrOverloadConsistency) {
    Q q;
    EXPECT_EQ(q.try_pop(), nullptr);

    q.push(7);
    auto p1 = q.try_pop();
    ASSERT_NE(p1, nullptr);
    EXPECT_EQ(*p1, 7);

    EXPECT_EQ(q.try_pop(), nullptr);
}
