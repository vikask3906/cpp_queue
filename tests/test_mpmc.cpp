#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <numeric>
#include <set>

#include "blocking_mpmc_unbounded/defs.hpp"

// ============================================================================
// Blocking MPMC Unbounded Tests
// ============================================================================

TEST(MPMCUnbounded, BasicPushPop) {
    tsfqueue::__impl::blocking_mpmc_unbounded<int> q;
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

TEST(MPMCUnbounded, PopFromEmpty) {
    tsfqueue::__impl::blocking_mpmc_unbounded<int> q;
    int val = -1;
    EXPECT_FALSE(q.try_pop(val));
    EXPECT_TRUE(q.empty());
}

TEST(MPMCUnbounded, WaitAndPop) {
    tsfqueue::__impl::blocking_mpmc_unbounded<int> q;

    // Push in one thread, wait_and_pop in another
    std::thread producer([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        q.push(42);
    });

    int val = 0;
    q.wait_and_pop(val);  // Should block until producer pushes
    EXPECT_EQ(val, 42);

    producer.join();
}

TEST(MPMCUnbounded, SharedPtrPop) {
    tsfqueue::__impl::blocking_mpmc_unbounded<int> q;
    q.push(99);

    auto result = q.try_pop();
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(*result, 99);

    auto empty_result = q.try_pop();
    EXPECT_EQ(empty_result, nullptr);
}

TEST(MPMCUnbounded, BurstAndDrain) {
    tsfqueue::__impl::blocking_mpmc_unbounded<int> q;
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

TEST(MPMCUnbounded, SingleProducerSingleConsumer) {
    constexpr int N = 50000;
    tsfqueue::__impl::blocking_mpmc_unbounded<int> q;

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
}

TEST(MPMCUnbounded, MultipleProducersSingleConsumer) {
    constexpr int NUM_PRODUCERS = 4;
    constexpr int ITEMS_PER_PRODUCER = 10000;
    constexpr int TOTAL = NUM_PRODUCERS * ITEMS_PER_PRODUCER;

    tsfqueue::__impl::blocking_mpmc_unbounded<int> q;

    // Each producer pushes values in range [id*ITEMS, (id+1)*ITEMS)
    std::vector<std::thread> producers;
    for (int id = 0; id < NUM_PRODUCERS; id++) {
        producers.emplace_back([&q, id]() {
            int base = id * ITEMS_PER_PRODUCER;
            for (int i = 0; i < ITEMS_PER_PRODUCER; i++) {
                q.push(base + i);
            }
        });
    }

    // Single consumer collects all values
    std::vector<int> results;
    results.reserve(TOTAL);
    std::thread consumer([&]() {
        for (int i = 0; i < TOTAL; i++) {
            int val = 0;
            q.wait_and_pop(val);
            results.push_back(val);
        }
    });

    for (auto &t : producers) t.join();
    consumer.join();

    // All items must be received (order may vary due to multiple producers)
    ASSERT_EQ(results.size(), TOTAL);

    std::sort(results.begin(), results.end());
    for (int i = 0; i < TOTAL; i++) {
        EXPECT_EQ(results[i], i);
    }
}

TEST(MPMCUnbounded, MultipleProducersMultipleConsumers) {
    constexpr int NUM_PRODUCERS = 4;
    constexpr int NUM_CONSUMERS = 4;
    constexpr int ITEMS_PER_PRODUCER = 10000;
    constexpr int TOTAL = NUM_PRODUCERS * ITEMS_PER_PRODUCER;
    constexpr int ITEMS_PER_CONSUMER = TOTAL / NUM_CONSUMERS;

    tsfqueue::__impl::blocking_mpmc_unbounded<int> q;

    std::vector<std::thread> producers;
    for (int id = 0; id < NUM_PRODUCERS; id++) {
        producers.emplace_back([&q, id]() {
            int base = id * ITEMS_PER_PRODUCER;
            for (int i = 0; i < ITEMS_PER_PRODUCER; i++) {
                q.push(base + i);
            }
        });
    }

    // Each consumer grabs its share
    std::vector<std::vector<int>> consumer_results(NUM_CONSUMERS);
    std::vector<std::thread> consumers;
    for (int id = 0; id < NUM_CONSUMERS; id++) {
        consumers.emplace_back([&q, &consumer_results, id]() {
            for (int i = 0; i < ITEMS_PER_CONSUMER; i++) {
                int val = 0;
                q.wait_and_pop(val);
                consumer_results[id].push_back(val);
            }
        });
    }

    for (auto &t : producers) t.join();
    for (auto &t : consumers) t.join();

    // Merge all consumer results and verify completeness
    std::vector<int> all_results;
    for (auto &cr : consumer_results) {
        all_results.insert(all_results.end(), cr.begin(), cr.end());
    }

    ASSERT_EQ(all_results.size(), TOTAL);

    std::sort(all_results.begin(), all_results.end());
    for (int i = 0; i < TOTAL; i++) {
        EXPECT_EQ(all_results[i], i);
    }
}
