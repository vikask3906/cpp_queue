#include <cassert>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "lockfree_spsc_unbounded/defs.hpp"

using Queue = tsfqueue::__impl::lockfree_spsc_unbounded<int>;
using StringQueue = tsfqueue::__impl::lockfree_spsc_unbounded<std::string>;

// ============================================================================
// Test 1: Basic single-threaded push/pop
// ============================================================================
void test_basic_push_pop() {
    std::cout << "[Test 1] Basic push/pop... ";
    Queue q;

    assert(q.empty());
    assert(q.size() == 0);

    q.push(10);
    q.push(20);
    q.push(30);

    assert(!q.empty());
    assert(q.size() == 3);

    int val = 0;
    assert(q.try_pop(val) && val == 10);
    assert(q.try_pop(val) && val == 20);
    assert(q.try_pop(val) && val == 30);

    assert(q.empty());
    assert(!q.try_pop(val));  // should fail, queue is empty

    std::cout << "PASSED\n";
}

// ============================================================================
// Test 2: Peek without removing
// ============================================================================
void test_peek() {
    std::cout << "[Test 2] Peek... ";
    Queue q;

    int val = 0;
    assert(!q.peek(val));  // empty queue peek should fail

    q.push(42);
    q.push(99);

    assert(q.peek(val) && val == 42);  // peek returns front
    assert(q.peek(val) && val == 42);  // peek doesn't remove
    assert(q.size() == 2);             // size unchanged

    assert(q.try_pop(val) && val == 42);  // now actually pop
    assert(q.peek(val) && val == 99);     // next element

    std::cout << "PASSED\n";
}

// ============================================================================
// Test 3: Emplace with non-trivial type
// ============================================================================
void test_emplace() {
    std::cout << "[Test 3] Emplace... ";
    StringQueue q;

    q.emplace("hello");
    q.emplace("world");

    std::string val;
    assert(q.try_pop(val) && val == "hello");
    assert(q.try_pop(val) && val == "world");
    assert(q.empty());

    std::cout << "PASSED\n";
}

// ============================================================================
// Test 4: SPSC concurrent correctness (1 producer + 1 consumer)
// Pushes N integers, verifies all arrive in order
// ============================================================================
void test_spsc_concurrent() {
    std::cout << "[Test 4] SPSC concurrent (100000 elements)... ";
    constexpr int N = 100000;
    Queue q;

    // Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < N; i++) {
            q.push(i);
        }
    });

    // Consumer thread
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

    // Verify all elements arrived in order
    assert(results.size() == N);
    for (int i = 0; i < N; i++) {
        assert(results[i] == i);
    }
    assert(q.empty());

    std::cout << "PASSED\n";
}

// ============================================================================
// Test 5: Empty queue behavior
// ============================================================================
void test_empty_queue() {
    std::cout << "[Test 5] Empty queue edge cases... ";
    Queue q;

    int val = -1;
    assert(q.empty());
    assert(q.size() == 0);
    assert(!q.try_pop(val));
    assert(!q.peek(val));

    // Push and pop a single element, then verify empty again
    q.push(1);
    assert(!q.empty());
    assert(q.try_pop(val) && val == 1);
    assert(q.empty());

    std::cout << "PASSED\n";
}

// ============================================================================
// Test 6: Large burst then drain
// ============================================================================
void test_burst_drain() {
    std::cout << "[Test 6] Burst push then drain... ";
    Queue q;

    for (int i = 0; i < 10000; i++) {
        q.push(i);
    }
    assert(q.size() == 10000);

    int val = 0;
    for (int i = 0; i < 10000; i++) {
        assert(q.try_pop(val) && val == i);
    }
    assert(q.empty());

    std::cout << "PASSED\n";
}

// ============================================================================
int main() {
    std::cout << "=== lockfree_spsc_unbounded Validation ===\n\n";

    test_basic_push_pop();
    test_peek();
    test_emplace();
    test_spsc_concurrent();
    test_empty_queue();
    test_burst_drain();

    std::cout << "\n=== ALL TESTS PASSED ===\n";
    return 0;
}
