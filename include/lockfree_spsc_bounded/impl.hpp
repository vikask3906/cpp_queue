#ifndef LOCKFREE_SPSC_BOUNDED_IMPL
#define LOCKFREE_SPSC_BOUNDED_IMPL

namespace tsfqueue::__impl {

// ─────────────────────────────────────────────────────────────────────────────
// try_push
//
// Producer-only. Wait-free — returns false immediately if the queue is full.
//
// Memory ordering:
//   tail.load(relaxed)  — only the producer ever writes tail; a relaxed load
//                         is safe because there is no concurrent writer.
//   head.load(acquire)  — we need to *see* all consumer writes that happened
//                         before the consumer stored head.  acquire pairs with
//                         the consumer's head.store(release).
//   tail.store(release) — publishes the newly written slot to the consumer.
//                         pairs with the consumer's tail.load(acquire).
// ─────────────────────────────────────────────────────────────────────────────
template <typename T, size_t Capacity>
bool lockfree_spsc_bounded<T, Capacity>::try_push(T value) {
    const size_t current_tail = tail.load(std::memory_order_relaxed);
    const size_t next_tail    = (current_tail + 1) % buffer_size;

    // Fast path: use the cached head to avoid a cross-core atomic read.
    if (next_tail == head_cache) {
        // Cached value says full — re-read the authoritative atomic.
        head_cache = head.load(std::memory_order_acquire);
        if (next_tail == head_cache) {
            return false; // truly full
        }
    }

    arr[current_tail] = std::move(value); // write data into the slot
    tail.store(next_tail, std::memory_order_release); // publish to consumer
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// wait_and_push
//
// Producer-only. Lock-free (not wait-free) — spins until try_push succeeds.
//
// KEY FIX vs. your original: we do NOT call try_push(std::move(value)).
// Passing a moved-from object on the first failed attempt would leave `value`
// in a valid-but-unspecified state for all subsequent retries.  Instead we
// keep `value` alive here and move into the slot only inside try_push at the
// point of success.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T, size_t Capacity>
void lockfree_spsc_bounded<T, Capacity>::wait_and_push(T value) {
    int spin_count = 0;
    while (true) {
        // Pass by reference internally so value is NOT consumed on failure.
        const size_t current_tail = tail.load(std::memory_order_relaxed);
        const size_t next_tail    = (current_tail + 1) % buffer_size;

        if (next_tail == head_cache) {
            head_cache = head.load(std::memory_order_acquire);
        }

        if (next_tail != head_cache) {
            arr[current_tail] = std::move(value); // consume value exactly once
            tail.store(next_tail, std::memory_order_release);
            return;
        }

        // Queue is full — back off.
        if (spin_count++ < 100) {
            // Busy-spin: ultra-low latency for very brief waits.
        } else {
            std::this_thread::yield(); // be nice to the OS scheduler
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// try_emplace
//
// Producer-only. Constructs T in-place from forwarded args — avoids the
// extra move that try_push(T value) would incur when the caller holds
// constructor arguments rather than an already-constructed T.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T, size_t Capacity>
template <typename... Args>
bool lockfree_spsc_bounded<T, Capacity>::try_emplace(Args &&...args) {
    const size_t current_tail = tail.load(std::memory_order_relaxed);
    const size_t next_tail    = (current_tail + 1) % buffer_size;

    if (next_tail == head_cache) {
        head_cache = head.load(std::memory_order_acquire);
        if (next_tail == head_cache) {
            return false;
        }
    }

    // Construct directly into the slot — no temporary T created.
    // std::construct_at is C++20; for C++17 use placement-new instead:
    //   new (&arr[current_tail]) T(std::forward<Args>(args)...);
    std::construct_at(&arr[current_tail], std::forward<Args>(args)...);
    tail.store(next_tail, std::memory_order_release);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// try_pop
//
// Consumer-only. Wait-free — returns false immediately if the queue is empty.
//
// Memory ordering:
//   head.load(relaxed)  — only the consumer ever writes head.
//   tail.load(acquire)  — pairs with producer's tail.store(release) so we see
//                         the data written before the producer published tail.
//   head.store(release) — publishes the freed slot back to the producer.
//                         pairs with producer's head.load(acquire).
// ─────────────────────────────────────────────────────────────────────────────
template <typename T, size_t Capacity>
bool lockfree_spsc_bounded<T, Capacity>::try_pop(T &value) {
    const size_t current_head = head.load(std::memory_order_relaxed); // FIX: was std::mutex memory_order_relaxed

    if (current_head == tail_cache) {
        tail_cache = tail.load(std::memory_order_acquire);
        if (current_head == tail_cache) {
            return false; // truly empty
        }
    }

    value = std::move(arr[current_head]);
    const size_t next_head = (current_head + 1) % buffer_size;
    head.store(next_head, std::memory_order_release); // publish freed slot
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// wait_and_pop
// ─────────────────────────────────────────────────────────────────────────────
template <typename T, size_t Capacity>
void lockfree_spsc_bounded<T, Capacity>::wait_and_pop(T &value) {
    int spin_count = 0;
    while (!try_pop(value)) {
        if (spin_count++ < 100) {
            // Busy-spin
        } else {
            std::this_thread::yield();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// peek
//
// Consumer-only. Reads the front element without removing it.
// Correct in SPSC because the consumer is the only thread that can pop —
// the element at head cannot disappear between peek() and the consumer's
// own subsequent action.  In MPMC, another consumer could pop between
// peek() and the caller using the value, making it stale.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T, size_t Capacity>
bool lockfree_spsc_bounded<T, Capacity>::peek(T &value) const {
    const size_t current_head = head.load(std::memory_order_relaxed);

    if (current_head == tail_cache) {
        // const_cast needed because tail_cache is mutable state (a cache).
        // Alternatively declare tail_cache as mutable in the class.
        const_cast<size_t &>(tail_cache) = tail.load(std::memory_order_acquire);
        if (current_head == tail_cache) {
            return false;
        }
    }

    value = arr[current_head]; // copy, not move — we are only peeking
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// empty
// ─────────────────────────────────────────────────────────────────────────────
template <typename T, size_t Capacity>
bool lockfree_spsc_bounded<T, Capacity>::empty() const {
    const size_t h = head.load(std::memory_order_acquire);
    const size_t t = tail.load(std::memory_order_acquire);
    return h == t;
}

// ─────────────────────────────────────────────────────────────────────────────
// size
//
// Returns a snapshot of the number of elements.  The value may be stale
// immediately after it is returned.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T, size_t Capacity>
size_t lockfree_spsc_bounded<T, Capacity>::size() const {
    const size_t h = head.load(std::memory_order_acquire);
    const size_t t = tail.load(std::memory_order_acquire);
    return (t - h + buffer_size) % buffer_size;
}

} // namespace tsfqueue::__impl

#endif // LOCKFREE_SPSC_BOUNDED_IMPL



// #ifndef LOCKFREE_SPSC_BOUNDED_IMPL_CT
// #define LOCKFREE_SPSC_BOUNDED_IMPL_CT

// // No #include "defs.hpp" needed — this file is always included from the
// // bottom of defs.hpp, after the class definition is already visible.

// namespace tsfqueue::__impl {

// template <typename T, size_t Capacity>
// void lockfree_spsc_bounded<T, Capacity>::wait_and_push(T value) {
//     int spin_count = 0;
    
//     while (!try_push(std::move(value))) {
//         // Spin aggressively for the first 100 attempts (takes fractions of a microsecond)
//         if (spin_count < 100) {
//             spin_count++; // Pure busy-wait, extremely low latency
//         } else {
//             // If it's taking too long, play nice with the OS and yield
//             std::this_thread::yield();
//         }
//     }
// }

// template <typename T, size_t Capacity>
// bool lockfree_spsc_bounded<T, Capacity>::try_push(T value) {
//     const size_t current_tail = tail.load(std::memory_order_relaxed);
//     const size_t next_tail = (current_tail + 1) % buffer_size;
    
//     if(next_tail == head_cache)
//     {
//         head_cache= head.load(std::memory_order_acquire);
//         if (next_tail == head_cache)
//         {
//             return false;
//         }
//     }
//     arr[current_tail]=std::move(value);
//     tail.store(next_tail,std::memory_order_release);
//     return true;

// }

// template <typename T, size_t Capacity>
// template <typename... Args>
// bool lockfree_spsc_bounded<T, Capacity>::try_emplace(Args &&...args) {
//      const size_t current_tail = tail.load(std::memory_order_relaxed);
//     const size_t next_tail = (current_tail + 1) % buffer_size;
    
//     if(next_tail == head_cache)
//     {
//         head_cache= head.load(std::memory_order_acquire);
//         if (next_tail == head_cache)
//         {
//             return false;
//         }
//     }
//     //arr[current_tail]=std::move(value);
//     arr[current_tail] =  T(std::forward<Args>(args)...);
//     tail.store(next_tail,std::memory_order_release);
//     return true;
// }

// template <typename T, size_t Capacity>
// bool lockfree_spsc_bounded<T, Capacity>::try_pop(T &value) {
//     const size_t current_head = head.load(std::mutex memory_order_relaxed);
    
//     if (current_head==tail_cache)
//     {
//         tail_cache = tail.load(std::memory_order_acquire);
//         if (tail_cache==current_head)
//         {
//             return false;
//         }
//     }

//     const size_t next_head = (current_head+1)%buffer_size;
//     value = std::move(arr[current_head]);
//     // head pointer ko update karna hai
//     head.store(next_head,std::memory_order_release);
//     return true;
// }

// template <typename T, size_t Capacity>
// void lockfree_spsc_bounded<T, Capacity>::wait_and_pop(T &value) {
//     size_t spin_count =0;
    
//     while (!try_pop(value))
//     {
//         if (spin_count<100){
//             spin_count++;
//         }
//         else
//         {
//             std::this_thread::yield();
//         }
//     }
    

// }


// // further do benchmarking and how to improve this further

// template <typename T, size_t Capacity>
// bool lockfree_spsc_bounded<T, Capacity>::peek(T &value) {
//     const size_t current_head = head.load(std::memory_order_relaxed);
    
//     if (current_head==tail_cache)
//     {
//         tail_cache=tail.load(std::memory_order_acquire);
//         if (current_head==tail_cache)
//         {
//             return false;
//         }
//     }
//     value = arr[current_head];
//     return true;
// }

// template <typename T, size_t Capacity> 
// bool lockfree_spsc_bounded<T, Capacity>::empty() {
//     const size_t current_head = head.load(std::memory_order_acquire);
//     if (current_head==tail_cache)
//     {
//         tail_cache=tail.load(std::memory_order_acquire);
//         if (current_head==tail_cache)
//         {
//             return true;
//         }
//     }
//     return false;
// }

// template <typename T, size_t Capacity> 
// size_t lockfree_spsc_bounded<T, Capacity>::size() {
//     const size_t current_head = head.load(std::memory_order_acquire);
//     const size_t current_tail = tail.load(std::memory_order_acquire);
//     return (current_tail-current_head+buffer_size)%buffer_size;
// }

// } // namespace tsfqueue::__impl

// #endif

// // 1. Add static asserts
// // 2. Add emplace_back using perfect forwarding and variadic templates (you
// // can use this in push then)
// // 3. Add size() function
// // 4. Any more suggestions ??