#include <cassert>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "lockfree_spsc_bounded/defs.hpp"
#include "lockfree_spsc_unbounded/defs.hpp"
#include "blocking_mpmc_unbounded/defs.hpp"

// ============================================================================
// Baseline: Standard Library Queue with Mutex
// ============================================================================
template <typename T>
class StdMutexQueue {
private:
    std::queue<T> q;
    std::mutex mtx;
    std::condition_variable cv;
public:
    void push(T value) {
        std::lock_guard<std::mutex> lock(mtx);
        q.push(std::move(value));
        cv.notify_one();
    }
    void wait_and_pop(T &ref) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this]() { return !q.empty(); });
        ref = std::move(q.front());
        q.pop();
    }
    void wait_and_push(T value) { push(std::move(value)); }
};

// ============================================================================
// Test Suite for lockfree_spsc_bounded
// ============================================================================
void run_tests() {
    std::cout << "=== lockfree_spsc_bounded: Validation Tests ===\n\n";

    // Test 1: Basic push/pop
    {
        std::cout << "[Test 1] Basic push/pop... ";
        tsfqueue::__impl::lockfree_spsc_bounded<int, 8> q;
        assert(q.empty());
        assert(q.size() == 0);

        assert(q.try_push(10));
        assert(q.try_push(20));
        assert(q.try_push(30));
        assert(!q.empty());
        assert(q.size() == 3);

        int val = 0;
        assert(q.try_pop(val) && val == 10);
        assert(q.try_pop(val) && val == 20);
        assert(q.try_pop(val) && val == 30);
        assert(q.empty());
        assert(!q.try_pop(val));
        std::cout << "PASSED\n";
    }

    // Test 2: Bounded capacity (fills up)
    {
        std::cout << "[Test 2] Bounded capacity... ";
        tsfqueue::__impl::lockfree_spsc_bounded<int, 4> q;
        assert(q.try_push(1));
        assert(q.try_push(2));
        assert(q.try_push(3));
        assert(q.try_push(4));
        assert(!q.try_push(5));  // Should fail — queue is full (4 items, capacity 4)
        assert(q.size() == 4);

        int val = 0;
        assert(q.try_pop(val) && val == 1);
        assert(q.try_push(5));  // Now there's space
        std::cout << "PASSED\n";
    }

    // Test 3: Peek
    {
        std::cout << "[Test 3] Peek... ";
        tsfqueue::__impl::lockfree_spsc_bounded<int, 8> q;
        int val = 0;
        assert(!q.peek(val));
        q.try_push(42);
        q.try_push(99);
        assert(q.peek(val) && val == 42);
        assert(q.peek(val) && val == 42);  // peek doesn't remove
        assert(q.size() == 2);
        std::cout << "PASSED\n";
    }

    // Test 4: Emplace with strings
    {
        std::cout << "[Test 4] Emplace... ";
        tsfqueue::__impl::lockfree_spsc_bounded<std::string, 8> q;
        assert(q.try_emplace("hello"));
        assert(q.try_emplace("world"));
        std::string val;
        assert(q.try_pop(val) && val == "hello");
        assert(q.try_pop(val) && val == "world");
        std::cout << "PASSED\n";
    }

    // Test 5: Wrap-around (ring buffer correctness)
    {
        std::cout << "[Test 5] Ring buffer wrap-around... ";
        tsfqueue::__impl::lockfree_spsc_bounded<int, 4> q;
        int val = 0;
        // Fill and drain several times to force wrap-around
        for (int round = 0; round < 5; round++) {
            for (int i = 0; i < 4; i++) {
                assert(q.try_push(round * 10 + i));
            }
            assert(!q.try_push(999));  // full
            for (int i = 0; i < 4; i++) {
                assert(q.try_pop(val) && val == round * 10 + i);
            }
            assert(q.empty());
        }
        std::cout << "PASSED\n";
    }

    // Test 6: Concurrent SPSC (1 producer + 1 consumer)
    {
        std::cout << "[Test 6] SPSC concurrent (100000 elements)... ";
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

        assert(results.size() == N);
        for (int i = 0; i < N; i++) {
            assert(results[i] == i);
        }
        assert(q.empty());
        std::cout << "PASSED\n";
    }

    std::cout << "\n=== ALL TESTS PASSED ===\n\n";
}

// ============================================================================
// Benchmark Runner
// ============================================================================
// Benchmark for queues with push() + wait_and_pop() (unbounded/blocking)
template <typename QueueType>
double run_benchmark_unbounded(const std::string& name, int num_elements) {
    QueueType q;

    auto start = std::chrono::high_resolution_clock::now();

    std::thread producer([&]() {
        for (int i = 0; i < num_elements; ++i) {
            q.push(i);
        }
    });

    std::thread consumer([&]() {
        int val = 0;
        for (int i = 0; i < num_elements; ++i) {
            q.wait_and_pop(val);
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = end - start;
    double throughput = (num_elements / (duration.count() / 1000.0)) / 1000000.0;

    std::cout << name << ":\n";
    std::cout << "  -> Time taken: " << duration.count() << " ms\n";
    std::cout << "  -> Throughput: " << throughput << " million ops/sec\n\n";

    return throughput;
}

// Benchmark for queues with wait_and_push() + wait_and_pop() (bounded)
template <typename QueueType>
double run_benchmark_bounded(const std::string& name, int num_elements) {
    QueueType q;

    auto start = std::chrono::high_resolution_clock::now();

    std::thread producer([&]() {
        for (int i = 0; i < num_elements; ++i) {
            q.wait_and_push(i);
        }
    });

    std::thread consumer([&]() {
        int val = 0;
        for (int i = 0; i < num_elements; ++i) {
            q.wait_and_pop(val);
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = end - start;
    double throughput = (num_elements / (duration.count() / 1000.0)) / 1000000.0;

    std::cout << name << ":\n";
    std::cout << "  -> Time taken: " << duration.count() << " ms\n";
    std::cout << "  -> Throughput: " << throughput << " million ops/sec\n\n";

    return throughput;
}

int main() {
    // Phase 1: Run correctness tests
    run_tests();

    // Phase 2: Benchmarks
    const int N = 5'000'000;
    std::cout << "=== Benchmark: " << N << " integers ===\n\n";

    double t1 = run_benchmark_unbounded<StdMutexQueue<int>>(
        "1. Baseline (std::queue + std::mutex)", N);

    double t2 = run_benchmark_unbounded<tsfqueue::__impl::blocking_mpmc_unbounded<int>>(
        "2. Custom Blocking MPMC Unbounded", N);

    double t3 = run_benchmark_unbounded<tsfqueue::__impl::lockfree_spsc_unbounded<int>>(
        "3. Lock-Free SPSC Unbounded", N);

    double t4 = run_benchmark_bounded<tsfqueue::__impl::lockfree_spsc_bounded<int, 4096>>(
        "4. Lock-Free SPSC Bounded (ring buffer)", N);

    // Summary
    std::cout << "=== Summary ===\n";
    std::cout << "Bounded vs Unbounded SPSC speedup: " << (t4 / t3) << "x\n";
    std::cout << "Bounded vs Blocking MPMC speedup:  " << (t4 / t2) << "x\n";
    std::cout << "Bounded vs std::queue speedup:     " << (t4 / t1) << "x\n";

    return 0;
}
