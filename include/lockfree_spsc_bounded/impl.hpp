#ifndef LOCKFREE_SPSC_BOUNDED_IMPL_CT
#define LOCKFREE_SPSC_BOUNDED_IMPL_CT

// No #include "defs.hpp" needed — this file is always included from the
// bottom of defs.hpp, after the class definition is already visible.

namespace tsfqueue::__impl {

template <typename T, size_t Capacity>
void lockfree_spsc_bounded<T, Capacity>::wait_and_push(T value) {
    int spin_count = 0;
    
    while (!try_push(std::move(value))) {
        // Spin aggressively for the first 100 attempts (takes fractions of a microsecond)
        if (spin_count < 100) {
            spin_count++; // Pure busy-wait, extremely low latency
        } else {
            // If it's taking too long, play nice with the OS and yield
            std::this_thread::yield();
        }
    }
}

template <typename T, size_t Capacity>
bool lockfree_spsc_bounded<T, Capacity>::try_push(T value) {
    const size_t current_tail = tail.load(std::memory_order_relaxed);
    const size_t next_tail = (current_tail + 1) % buffer_size;
    
    if(next_tail == head_cache)
    {
        head_cache= head.load(std::memory_order_acquire);
        if (next_tail == head_cache)
        {
            return false;
        }
    }
    arr[current_tail]=std::move(value);
    tail.store(next_tail,std::memory_order_release);
    return true;

}

template <typename T, size_t Capacity>
template <typename... Args>
bool lockfree_spsc_bounded<T, Capacity>::try_emplace(Args &&...args) {
    const size_t current_tail = tail.load(std::memory_order_relaxed);
    const size_t next_tail = (current_tail + 1) % buffer_size;

    if (next_tail == head_cache) {
        head_cache = head.load(std::memory_order_acquire);
        if (next_tail == head_cache) {
            return false;
        }
    }

    // Construct T directly in the array using forwarded constructor arguments
    arr[current_tail] = T(std::forward<Args>(args)...);
    tail.store(next_tail, std::memory_order_release);
    return true;
}

template <typename T, size_t Capacity>
bool lockfree_spsc_bounded<T, Capacity>::try_pop(T &value) {
    const size_t current_head = head.load(std::memory_order_relaxed);
    
    if (current_head==tail_cache)
    {
        tail_cache = tail.load(std::memory_order_acquire);
        if (tail_cache==current_head)
        {
            return false;
        }
    }

    const size_t next_head = (current_head+1)%buffer_size;
    value = std::move(arr[current_head]);
    // head pointer ko update karna hai
    head.store(next_head,std::memory_order_release);
    return true;
}

template <typename T, size_t Capacity>
void lockfree_spsc_bounded<T, Capacity>::wait_and_pop(T &value) {
    size_t spin_count =0;
    
    while (!try_pop(value))
    {
        if (spin_count<100){
            spin_count++;
        }
        else
        {
            std::this_thread::yield();
        }
    }
    

}

// Peek at the front element without removing it
template <typename T, size_t Capacity>
bool lockfree_spsc_bounded<T, Capacity>::peek(T &value) {
    const size_t current_head = head.load(std::memory_order_relaxed);

    if (current_head == tail_cache) {
        tail_cache = tail.load(std::memory_order_acquire);
        if (current_head == tail_cache) {
            return false;
        }
    }

    value = arr[current_head];  // copy, do NOT move
    return true;
}

// Check if queue is empty
template <typename T, size_t Capacity> 
bool lockfree_spsc_bounded<T, Capacity>::empty() {
    return head.load(std::memory_order_acquire) ==
           tail.load(std::memory_order_acquire);
}

// Count elements currently in the queue (snapshot, may be stale)
template <typename T, size_t Capacity> 
size_t lockfree_spsc_bounded<T, Capacity>::size() {
    const size_t h = head.load(std::memory_order_acquire);
    const size_t t = tail.load(std::memory_order_acquire);
    return (t - h + buffer_size) % buffer_size;
}

} // namespace tsfqueue::__impl

#endif

// 1. Add static asserts
// 2. Add emplace_back using perfect forwarding and variadic templates (you
// can use this in push then)
// 3. Add size() function
// 4. Any more suggestions ??