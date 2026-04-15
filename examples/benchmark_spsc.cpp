#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "blocking_mpmc_unbounded/defs.hpp"
#include "lockfree_spsc_unbounded/defs.hpp"

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
};

// ============================================================================
// Benchmark Runner
// ============================================================================
template <typename QueueType>
void run_benchmark(const std::string& name, int num_elements) {
    QueueType q;
    
    // We will measure the time it takes for both threads to finish their work
    auto start_time = std::chrono::high_resolution_clock::now();

    // Producer Thread
    std::thread producer([&]() {
        for (int i = 0; i < num_elements; ++i) {
            q.push(i);
        }
    });

    // Consumer Thread
    std::thread consumer([&]() {
        int val = 0;
        for (int i = 0; i < num_elements; ++i) {
            q.wait_and_pop(val);
            // Optional: avoid compiler optimizing out the loop
            if (val == -1) break; 
        }
    });

    producer.join();
    consumer.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = end_time - start_time;

    std::cout << name << ":\n";
    std::cout << "  -> Time taken: " << duration.count() << " ms\n";
    std::cout << "  -> Throughput: " << (num_elements / (duration.count() / 1000.0)) / 1000000.0 << " million ops/sec\n\n";
}

int main() {
    const int NUM_ELEMENTS = 5'000'000; // 5 million integers

    std::cout << "=== SPSC Queue Benchmark ===\n";
    std::cout << "Elements to process: " << NUM_ELEMENTS << "\n\n";

    // 1. Std Queue + Mutex
    run_benchmark<StdMutexQueue<int>>("1. Baseline (std::queue + std::mutex)", NUM_ELEMENTS);

    // 2. Custom Blocking MPMC
    using BlockingQueue = tsfqueue::__impl::blocking_mpmc_unbounded<int>;
    run_benchmark<BlockingQueue>("2. Custom Blocking MPMC Queue", NUM_ELEMENTS);

    // 3. Custom Lock-Free SPSC Unbounded
    using LockFreeSPSCQueue = tsfqueue::__impl::lockfree_spsc_unbounded<int>;
    run_benchmark<LockFreeSPSCQueue>("3. Custom Lock-Free SPSC Unbounded", NUM_ELEMENTS);

    return 0;
}
