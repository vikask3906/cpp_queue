#ifndef LOCKFREE_SPSC_BOUNDED_IMPL_CT
#define LOCKFREE_SPSC_BOUNDED_IMPL_CT

// No #include "defs.hpp" needed — this file is always included from the
// bottom of defs.hpp, after the class definition is already visible.

template <typename T, size_t Capacity>
using queue = tsfqueue::__impl::lockfree_spsc_bounded<T, Capacity>;

// ============================================================================
// Producer-only: try_push (wait-free)
// Returns false if queue is full, true if push succeeded
// ============================================================================
template <typename T, size_t Capacity>
bool queue<T, Capacity>::try_push(T value) {
    const size_t current_tail = tail.load(std::memory_order_relaxed);
    const size_t next_tail = (current_tail + 1) % buffer_size;

    // Check if full using the cached head first (avoids atomic read)
    if (next_tail == head_cache) {
        // Cache says full — re-read the real head to be sure
        head_cache = head.load(std::memory_order_acquire);
        if (next_tail == head_cache) {
            return false;  // truly full
        }
    }

    arr[current_tail] = std::move(value);
    tail.store(next_tail, std::memory_order_release);  // publish to consumer
    return true;
}

// ============================================================================
// Producer-only: wait_and_push (lock-free, not wait-free — spins until space)
// ============================================================================
template <typename T, size_t Capacity>
void queue<T, Capacity>::wait_and_push(T value) {
    while (!try_push(std::move(value))) {
        std::this_thread::yield();
    }
}

// ============================================================================
// Producer-only: try_emplace (wait-free, constructs T in-place)
// ============================================================================
template <typename T, size_t Capacity>
template <typename... Args>
bool queue<T, Capacity>::try_emplace(Args &&...args) {
    const size_t current_tail = tail.load(std::memory_order_relaxed);
    const size_t next_tail = (current_tail + 1) % buffer_size;

    if (next_tail == head_cache) {
        head_cache = head.load(std::memory_order_acquire);
        if (next_tail == head_cache) {
            return false;
        }
    }

    arr[current_tail] = T(std::forward<Args>(args)...);
    tail.store(next_tail, std::memory_order_release);
    return true;
}

// ============================================================================
// Consumer-only: try_pop (wait-free)
// Returns false if queue is empty, true if pop succeeded
// ============================================================================
template <typename T, size_t Capacity>
bool queue<T, Capacity>::try_pop(T &ref) {
    const size_t current_head = head.load(std::memory_order_relaxed);

    // Check if empty using the cached tail first (avoids atomic read)
    if (current_head == tail_cache) {
        // Cache says empty — re-read the real tail to be sure
        tail_cache = tail.load(std::memory_order_acquire);
        if (current_head == tail_cache) {
            return false;  // truly empty
        }
    }

    ref = std::move(arr[current_head]);
    head.store((current_head + 1) % buffer_size, std::memory_order_release);
    return true;
}

// ============================================================================
// Consumer-only: wait_and_pop (lock-free, spins until data available)
// ============================================================================
template <typename T, size_t Capacity>
void queue<T, Capacity>::wait_and_pop(T &ref) {
    while (!try_pop(ref)) {
        std::this_thread::yield();
    }
}

// ============================================================================
// Consumer-only: peek at front element without removing
// ============================================================================
template <typename T, size_t Capacity>
bool queue<T, Capacity>::peek(T &ref) {
    const size_t current_head = head.load(std::memory_order_relaxed);

    if (current_head == tail_cache) {
        tail_cache = tail.load(std::memory_order_acquire);
        if (current_head == tail_cache) {
            return false;
        }
    }

    ref = arr[current_head];  // copy, do NOT move (we're just peeking)
    return true;
}

// ============================================================================
// Check if queue is empty
// ============================================================================
template <typename T, size_t Capacity>
bool queue<T, Capacity>::empty() {
    return head.load(std::memory_order_acquire) ==
           tail.load(std::memory_order_acquire);
}

// ============================================================================
// Count elements currently in the queue
// WARNING: only a snapshot, may be stale by the time you use the result
// ============================================================================
template <typename T, size_t Capacity>
size_t queue<T, Capacity>::size() {
    const size_t h = head.load(std::memory_order_acquire);
    const size_t t = tail.load(std::memory_order_acquire);
    // Handle wrap-around: if tail >= head, simple subtraction
    // If tail < head (wrapped), add buffer_size
    return (t - h + buffer_size) % buffer_size;
}

#endif

// 1. Add static asserts
// 2. Add emplace_back using perfect forwarding and variadic templates (you
// can use this in push then)
// 3. Add size() function
// 4. Any more suggestions ??