#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

#include "lockfree_spsc_unbounded/defs.hpp"
#include "lockfree_spsc_bounded/defs.hpp"

// ============================================================================
// SPSC Unbounded Tests
// ============================================================================

TEST(SPSCUnbounded, BasicPushPop) {
    tsfqueue::__impl::lockfree_spsc_unbounded<int> q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0);

    q.push(10);
    q.push(20);
    q.push(30);

    EXPECT_FALSE(q.empty());
    EXPECT_EQ(q.size(), 3);

    int val = 0;
    EXPECT_TRUE(q.try_pop(val));
    EXPECT_EQ(val, 10);
    EXPECT_TRUE(q.try_pop(val));
    EXPECT_EQ(val, 20);
    EXPECT_TRUE(q.try_pop(val));
    EXPECT_EQ(val, 30);

    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.try_pop(val));
}

TEST(SPSCUnbounded, Peek) {
    tsfqueue::__impl::lockfree_spsc_unbounded<int> q;
    int val = 0;
    EXPECT_FALSE(q.peek(val));

    q.push(42);
    q.push(99);

    EXPECT_TRUE(q.peek(val));
    EXPECT_EQ(val, 42);

    // Peek should NOT remove the element
    EXPECT_TRUE(q.peek(val));
    EXPECT_EQ(val, 42);
    EXPECT_EQ(q.size(), 2);
}

TEST(SPSCUnbounded, Emplace) {
    tsfqueue::__impl::lockfree_spsc_unbounded<std::string> q;
    q.emplace("hello");
    q.emplace("world");

    std::string val;
    EXPECT_TRUE(q.try_pop(val));
    EXPECT_EQ(val, "hello");
    EXPECT_TRUE(q.try_pop(val));
    EXPECT_EQ(val, "world");
}

TEST(SPSCUnbounded, PopFromEmpty) {
    tsfqueue::__impl::lockfree_spsc_unbounded<int> q;
    int val = -1;
    EXPECT_FALSE(q.try_pop(val));
    EXPECT_TRUE(q.empty());
}

TEST(SPSCUnbounded, BurstAndDrain) {
    tsfqueue::__impl::lockfree_spsc_unbounded<int> q;
    constexpr int N = 10000;

    for (int i = 0; i < N; i++) {
        q.push(i);
    }
    EXPECT_EQ(q.size(), N);

    for (int i = 0; i < N; i++) {
        int val = 0;
        EXPECT_TRUE(q.try_pop(val));
        EXPECT_EQ(val, i);
    }
    EXPECT_TRUE(q.empty());
}

TEST(SPSCUnbounded, ConcurrentStress) {
    constexpr int N = 100000;
    tsfqueue::__impl::lockfree_spsc_unbounded<int> q;

    std::thread producer([&]() {
        for (int i = 0; i < N; i++) {
            q.push(i);
        }
    });

    std::vector<int> results;
    results.reserve(N);
    std::thread consumer([&]() {
        for (int i = 0; i < N; i++) {
            int val = 0;
            q.wait_and_pop(val);
            results.push_back(val);
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(results.size(), N);
    for (int i = 0; i < N; i++) {
        EXPECT_EQ(results[i], i);
    }
    EXPECT_TRUE(q.empty());
}

// ============================================================================
// SPSC Bounded Tests
// ============================================================================

TEST(SPSCBounded, BasicPushPop) {
    tsfqueue::__impl::lockfree_spsc_bounded<int, 8> q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0);

    EXPECT_TRUE(q.try_push(10));
    EXPECT_TRUE(q.try_push(20));
    EXPECT_TRUE(q.try_push(30));

    EXPECT_FALSE(q.empty());
    EXPECT_EQ(q.size(), 3);

    int val = 0;
    EXPECT_TRUE(q.try_pop(val));
    EXPECT_EQ(val, 10);
    EXPECT_TRUE(q.try_pop(val));
    EXPECT_EQ(val, 20);
    EXPECT_TRUE(q.try_pop(val));
    EXPECT_EQ(val, 30);

    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.try_pop(val));
}

TEST(SPSCBounded, BoundedCapacityFull) {
    tsfqueue::__impl::lockfree_spsc_bounded<int, 4> q;

    EXPECT_TRUE(q.try_push(1));
    EXPECT_TRUE(q.try_push(2));
    EXPECT_TRUE(q.try_push(3));
    EXPECT_TRUE(q.try_push(4));
    EXPECT_FALSE(q.try_push(5));  // Queue is full
    EXPECT_EQ(q.size(), 4);

    int val = 0;
    EXPECT_TRUE(q.try_pop(val));
    EXPECT_EQ(val, 1);
    EXPECT_TRUE(q.try_push(5));   // Now there's space
}

TEST(SPSCBounded, Peek) {
    tsfqueue::__impl::lockfree_spsc_bounded<int, 8> q;
    int val = 0;
    EXPECT_FALSE(q.peek(val));

    q.try_push(42);
    q.try_push(99);

    EXPECT_TRUE(q.peek(val));
    EXPECT_EQ(val, 42);

    // Peek should NOT remove the element
    EXPECT_TRUE(q.peek(val));
    EXPECT_EQ(val, 42);
    EXPECT_EQ(q.size(), 2);
}

TEST(SPSCBounded, Emplace) {
    tsfqueue::__impl::lockfree_spsc_bounded<std::string, 8> q;

    EXPECT_TRUE(q.try_emplace("hello"));
    EXPECT_TRUE(q.try_emplace("world"));

    std::string val;
    EXPECT_TRUE(q.try_pop(val));
    EXPECT_EQ(val, "hello");
    EXPECT_TRUE(q.try_pop(val));
    EXPECT_EQ(val, "world");
}

TEST(SPSCBounded, RingBufferWrapAround) {
    tsfqueue::__impl::lockfree_spsc_bounded<int, 4> q;
    int val = 0;

    // Fill and drain several times to force index wrap-around
    for (int round = 0; round < 5; round++) {
        for (int i = 0; i < 4; i++) {
            EXPECT_TRUE(q.try_push(round * 10 + i));
        }
        EXPECT_FALSE(q.try_push(999));  // full

        for (int i = 0; i < 4; i++) {
            EXPECT_TRUE(q.try_pop(val));
            EXPECT_EQ(val, round * 10 + i);
        }
        EXPECT_TRUE(q.empty());
    }
}

TEST(SPSCBounded, PopFromEmpty) {
    tsfqueue::__impl::lockfree_spsc_bounded<int, 8> q;
    int val = -1;
    EXPECT_FALSE(q.try_pop(val));
    EXPECT_TRUE(q.empty());
}

TEST(SPSCBounded, ConcurrentStress) {
    constexpr int N = 100000;
    tsfqueue::__impl::lockfree_spsc_bounded<int, 1024> q;

    std::thread producer([&]() {
        for (int i = 0; i < N; i++) {
            q.wait_and_push(i);
        }
    });

    std::vector<int> results;
    results.reserve(N);
    std::thread consumer([&]() {
        for (int i = 0; i < N; i++) {
            int val = 0;
            q.wait_and_pop(val);
            results.push_back(val);
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(results.size(), N);
    for (int i = 0; i < N; i++) {
        EXPECT_EQ(results[i], i);
    }
    EXPECT_TRUE(q.empty());
}
