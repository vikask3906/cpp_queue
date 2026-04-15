#ifndef LOCKFREE_SPSC_BOUNDED_DEFS
#define LOCKFREE_SPSC_BOUNDED_DEFS

#include "utils.hpp"
#include <atomic>
#include <memory>
#include <thread>
#include <type_traits>

namespace tsfqueue::__impl {

template <typename T, size_t Capacity>
class lockfree_spsc_bounded {
    // Ring-buffer based bounded SPSC queue.
    //
    // Design notes:
    // - Capacity+1 slots are allocated so that tail==head means empty and
    //   (tail+1)%buffer_size==head means full — one slot is intentionally
    //   sacrificed to distinguish these two states without an extra flag.
    // - head and tail are std::atomic<size_t> on separate cache lines so that
    //   the producer (writes tail) and consumer (writes head) never share a
    //   cache line — this eliminates false sharing.
    // - head_cache (near tail) and tail_cache (near head) are local copies that
    //   each side caches so that the hot path avoids cross-core atomic reads on
    //   every iteration.  Only when the cached copy says "full" or "empty" does
    //   the thread re-acquire the authoritative atomic value.
    // - All memory is stack/inline (no heap allocation) — compile-time size.

    // ── Static assertions ──────────────────────────────────────────────────
    static_assert(std::is_move_constructible_v<T>,
                  "T must be move constructible");
    static_assert(Capacity > 0,
                  "Capacity must be greater than 0");
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two — "
                  "this allows replacing % buffer_size with & (buffer_size-1) "
                  "which is a single instruction vs. a division.");

    // ── Constants ──────────────────────────────────────────────────────────
    // One extra slot so tail==head ↔ empty, (next_tail)==head ↔ full.
    static constexpr size_t buffer_size = Capacity + 1;

    // Bitmask trick — valid only because Capacity is a power of two and
    // buffer_size = Capacity+1, which is NOT a power of two.
    // Therefore we cannot use & (buffer_size-1) as a modulo replacement here;
    // we keep explicit % buffer_size.  If you want the bitmask trick, use
    // Capacity as the buffer size and accept that a full queue holds Capacity
    // elements with no wasted slot (requires a separate bool or counter).

public:
    // ── Constructors / destructor ──────────────────────────────────────────
    lockfree_spsc_bounded() = default;

    // No copying or moving — the ring buffer is inline; copying would silently
    // duplicate in-flight indices and break the invariant.
    lockfree_spsc_bounded(const lockfree_spsc_bounded &) = delete;
    lockfree_spsc_bounded &operator=(const lockfree_spsc_bounded &) = delete;
    lockfree_spsc_bounded(lockfree_spsc_bounded &&)      = delete;
    lockfree_spsc_bounded &operator=(lockfree_spsc_bounded &&) = delete;

    // ── Producer API ──────────────────────────────────────────────────────
    // try_push: wait-free — returns false immediately if the queue is full.
    // Takes value by value so the caller's object is moved into the slot.
    bool try_push(T value);

    // wait_and_push: lock-free (spins) — blocks until space is available.
    // Value is taken by value for the same reason.
    void wait_and_push(T value);

    // try_emplace: construct T in-place from args — avoids an extra move
    // compared to try_push when constructing from heterogeneous arguments.
    template <typename... Args>
    bool try_emplace(Args &&...args);

    // ── Consumer API ──────────────────────────────────────────────────────
    bool try_pop(T &ref);
    void wait_and_pop(T &ref);

    // ── Query API ─────────────────────────────────────────────────────────
    // empty / size: snapshot only — may be stale by the time the caller acts.
    bool   empty() const;
    size_t size()  const;

    // peek: read the front without removing it.
    // Safe only in SPSC/MPSC (single consumer) because in MPMC another
    // consumer can pop the element between peek() and the caller's action.
    bool peek(T &ref) const;

private:
    // ── Data members ──────────────────────────────────────────────────────
    //
    // Layout on memory (64-byte cache lines assumed):
    //
    //   [cache line 0]  head       — written by consumer, read by producer
    //   [cache line 1]  tail_cache — consumer's cached copy of tail
    //   [cache line 2]  tail       — written by producer, read by consumer
    //   [cache line 3]  head_cache — producer's cached copy of head
    //   [cache line 4+] arr        — the actual data
    //
    // Grouping tail_cache next to head (instead of next to tail) keeps the
    // consumer's hot-path data on as few cache lines as possible, and
    // similarly for the producer.

    alignas(tsfqueue::__impl::cache_line_size) std::atomic<size_t> head{0};
    alignas(tsfqueue::__impl::cache_line_size) size_t tail_cache{0}; // consumer's cached tail

    alignas(tsfqueue::__impl::cache_line_size) std::atomic<size_t> tail{0};
    alignas(tsfqueue::__impl::cache_line_size) size_t head_cache{0}; // producer's cached head

    alignas(tsfqueue::__impl::cache_line_size) T arr[buffer_size];
};

} // namespace tsfqueue::__impl

#include "lockfree_spsc_bounded_impl.hpp"

#endif // LOCKFREE_SPSC_BOUNDED_DEFS


// #ifndef LOCKFREE_SPSC_BOUNDED_DEFS
// #define LOCKFREE_SPSC_BOUNDED_DEFS

// #include "utils.hpp"
// #include <atomic>
// #include <memory>
// #include <thread>
// #include <type_traits>

// namespace tsfqueue::__impl {
// template <typename T, size_t Capacity> class lockfree_spsc_bounded {
//   // For the implementation, we first take the size of the bounded queue from
//   // user inside the templates so that we can do compile time memory allocation.
//   // We have two atomic pointer, head and tail, tail for pushing the element and
//   // head for popping. We also add check tail == head for empty which means one
//   // redundant element during allocation. We keep head_cache and tail_cache as
//   // cached copies to have a cache efficient code (discuss with me for details).
//   // All the data members are cache aligned to prevent cache-line bouncing.
//   // The user is provided with both set of functions : try_pop() and try_push()
//   // for a wait-free code And wait_and_pop() and wait_and_push() for a lock-less
//   // code but not wait-free variant. Thus, the user is given a choice to choose
//   // among the preferred endpoints as per use case.

//   static_assert(std::is_move_constructible_v<T>,
//                 "T must be move constructible");
//   static_assert(Capacity > 0, "Capacity must be greater than 0");

// private:
//   // Add the private members :
//   // std::atomic<size_t> head;
//   // std::atomic<size_t> tail;
//   // size_t head_cache;
//   // size_t tail_cache;
//   // T arr[];
//   // static constexpr size_t capacity;
//   std::atomic<size_t> head;
//   std::atomic<sizr_t> tail;
//   size_t head_cache;
//   size_t tail_cache;
//   static constexpr size_t Capacity;
//   T arr[];


//   // Description of private members :
//   // 1. std::atomic<size_t> head is the atomic head pointer
//   // 2. std::atomic<size_t> tail is the atomic tail pointer
//   // 3. size_t head_cache is the cached head pointer
//   // 4. size_t tail_cache is the cached tail pointer
//   // 5. T arr[] compile time allocated array
//   // Cache align 1-5.
//   // 6. static constexpr size_t capcity to store the capcity for operations in
//   // functions Why static ?? Why constexpr ?? [Reason this]

//   // +1 because one slot is always wasted to distinguish full from empty
//   static constexpr size_t buffer_size = Capacity + 1;

//   // Cache-aligned to prevent false sharing
//   // Producer writes tail, consumer writes head — they must be on separate cache lines
//   alignas(tsfq::__impl::cache_line_size) std::atomic<size_t> head{0};
//   alignas(tsfq::__impl::cache_line_size) std::atomic<size_t> tail{0};

//   // Cached copies: producer caches head, consumer caches tail
//   // These avoid cross-core atomic reads on the hot path
//   alignas(tsfq::__impl::cache_line_size) size_t head_cache{0};
//   alignas(tsfq::__impl::cache_line_size) size_t tail_cache{0};

//   // The actual ring buffer — compile-time allocated
//   alignas(tsfq::__impl::cache_line_size) T arr[buffer_size];

// public:
//   // Public Member functions :
//   // Add appropriate constructors and destructors -> Add here only
//   // 1. void wait_and_push(value) : Busy wait until element is pushed
//   // 2. bool try_push(value) : Try to push if not full else leave (returns false
//   // if could not push else true)
//   // 3. void wait_and_pop(value ref) : Busy wait until we have atmost 1 elt and
//   // then pop it and store in reference
//   // 4. bool try_pop(value ref) : Try to pop and return false if failed bool
//   // 5. empty(void) : Checks if the queue is empty and return bool
//   // 6. bool peek(value ref) : Peek the top of the queue.
//   // Will work only in SPSC/MPSC why ?? [Reason this]
//   // 7. Add static asserts
//   // 8. Add emplace_back using perfect forwarding and variadic templates (you
//   // can use this in push then)
//   // 9. Add size() function
//   // 10. Any more suggestions ??
//   // 11. Why no shared_ptr ?? [Reason this]

//   // Constructor
//   lockfree_spsc_bounded() = default;

//   // No copying or moving allowed
//   lockfree_spsc_bounded(const lockfree_spsc_bounded &) = delete;
//   lockfree_spsc_bounded &operator=(const lockfree_spsc_bounded &) = delete;
//   lockfree_spsc_bounded(lockfree_spsc_bounded &&) = delete;
//   lockfree_spsc_bounded &operator=(lockfree_spsc_bounded &&) = delete;

//   // Push functions (producer-only)
//   bool try_push(T value);
//   void wait_and_push(T value);

//   template <typename... Args>
//   bool try_emplace(Args &&...args);

//   // Pop functions (consumer-only)
//   bool try_pop(T &ref);
//   void wait_and_pop(T &ref);

//   // Query functions
//   bool empty();
//   bool peek(T &ref);
//   size_t size();
// };
// } // namespace tsfqueue::__impl

// // Include the out-of-line template definitions
// #include "impl.hpp"

// #endif