#ifndef LOCKFREE_SPSC_UNBOUNDED_DEFS
#define LOCKFREE_SPSC_UNBOUNDED_DEFS

#include "utils.hpp"
#include <atomic>
#include <memory>
#include <thread>
#include <type_traits>

namespace tsfqueue::__impl {

template <typename T>
class lockfree_spsc_unbounded {
  
    // Linked-list based unbounded SPSC queue — lock-free, no mutexes.
    //
    // Design notes:
    // - Uses the same sentinel/stub pattern as blocking_mpmc_unbounded but
    //   without any locks.  Correctness relies entirely on acquire/release
    //   memory ordering on node::next (an std::atomic<node*>).
    //
    // - node::next is atomic because it is the *publication channel*: the
    //   producer writes next (release) to signal that data is ready, and the
    //   consumer reads next (acquire) to observe the data.  Without atomicity,
    //   the compiler or CPU could reorder the data write after the next write,
    //   causing the consumer to read uninitialised data.
    //
    // - head and tail are plain (non-atomic) pointers because each is written
    //   by exactly one thread (head by the consumer, tail by the producer) and
    //   read only by that same thread in the normal operation.  The cross-thread
    //   communication goes through node::next.
    //
    // - head and tail are on separate cache lines to prevent false sharing:
    //   the producer writes tail frequently; the consumer writes head
    //   frequently.  If they shared a cache line, each write would invalidate
    //   the other core's cache line — a classic false-sharing penalty.
    //
    // - No shared_ptr: in SPSC there is exactly one producer and one consumer.
    //   Ownership transfer is linear — once the consumer pops a node it
    //   immediately deletes it.  shared_ptr's reference counting overhead
    //   (atomic increment/decrement) would be wasted work.

    static_assert(std::is_move_constructible_v<T>,
                  "T must be move constructible");

private:
    using node = tsfqueue::__utils::Lockless_Node<T>;

    // head: consumer's pointer to the sentinel node (whose *next* holds the
    //       next value).  Written only by the consumer.
    // tail: producer's pointer to the current dummy/stub at the end.
    //       Written only by the producer.
    //
    // Separate cache lines prevent false sharing between producer and consumer.
    alignas(tsfqueue::__impl::cache_line_size) node *head;
    alignas(tsfqueue::__impl::cache_line_size) node *tail;

public:
    // ── Constructor / destructor ───────────────────────────────────────────
    lockfree_spsc_unbounded();
    ~lockfree_spsc_unbounded();

    // No copy or move — raw pointers into a heap-allocated list; copying would
    // create aliasing that breaks single-owner deletion in the destructor.
    lockfree_spsc_unbounded(const lockfree_spsc_unbounded &) = delete;
    lockfree_spsc_unbounded &operator=(const lockfree_spsc_unbounded &) = delete;
    lockfree_spsc_unbounded(lockfree_spsc_unbounded &&)      = delete;
    lockfree_spsc_unbounded &operator=(lockfree_spsc_unbounded &&) = delete;

    // ── Producer API ──────────────────────────────────────────────────────
    void push(T value);

    template <typename... Args>
    void emplace(Args &&...args);

    // ── Consumer API ──────────────────────────────────────────────────────
    bool try_pop(T &ref);
    void wait_and_pop(T &ref);

    // peek: safe in SPSC — the consumer is the only thread that can advance
    // head, so the element observed cannot be removed by another thread.
    bool peek(T &ref) const;

    // ── Query API ─────────────────────────────────────────────────────────
    bool   empty() const;
    size_t size()  const; // snapshot only — see impl for caveats
};

} // namespace tsfqueue::__impl

#include "lockfree_spsc_unbounded_impl.hpp"

#endif // LOCKFREE_SPSC_UNBOUNDED_DEFS



// #ifndef LOCKFREE_SPSC_UNBOUNDED_DEFS
// #define LOCKFREE_SPSC_UNBOUNDED_DEFS

// #include "utils.hpp"
// #include <atomic>
// #include <memory>
// #include <thread>
// #include <type_traits>

// namespace tsfqueue::__impl {
// template <typename T> class lockfree_spsc_unbounded {
//   // Works exactly same as the blocking_mpmc_unbounded queue (see this once)
//   // with tail pointer pointing to stub node and your head pointer updates as
//   // per the pushes. See the Lockless_Node in utils to understand the working.
//   // Note that the next pointers are atomic there. Why ?? [Reason this]
//   // Also the head and tail members are cache-aligned. Why ?? [Reason this] (ask
//   // me for details)

//   // [Copy of blocking_mpmc_unbounded]
//   // For the implementation, we start with a stub node and both head and tail
//   // are initialized to it. When we push, we make a new stub node, move the data
//   // into the current tail and then change the tail to the new stub. We have two
//   // methods : wait_and_pop() which waits on the queue and returns element &
//   // try_pop() which returns an element if queue is not empty otherwise returns
//   // some neutral element OR a false boolean whichever is applicable. Pop works
//   // by returning the data stored in head node and replacing head to its next
//   // node. We handle the empty queue gracefully as per the pop type.

//   static_assert(std::is_move_constructible_v<T>,
//                 "T must be move constructible");

// private:
//   using node = tsfqueue::__utils::Lockless_Node<T>;

//   // Cache-aligned to prevent false sharing between producer and consumer
//   alignas(tsfq::__impl::cache_line_size) node *head;
//   alignas(tsfq::__impl::cache_line_size) node *tail;

//   // Add the private members :
//   // 1. node* head;
//   // 2. node* tail;

//   // Description of priavte members :
//   // 1. node* head -> Pointer to the head node
//   // 2. node* tail -> Pointer to tail node
//   // 3. Cache align 1-2

// public:
//   // Public member functions :
//   // Add relevant constructors and destructors -> Add these here only
//   // 1. void push(value) : Pushes the value inside the queue, copies the value
//   // 2. void wait_and_pop(value ref) : Blocking wait on queue, returns value in
//   // the reference passed as parameter
//   // 3. bool try_pop(value ref) : Returns true and
//   // gives the value in reference passed, false otherwise
//   // 4. bool empty() : Returns
//   // whether the queue is empty or not at that instant
//   // 5. bool peek(value ref) : Returns the front/top element of queue in ref (false if empty queue)
//   // 6. Add static asserts
//   // 7. Add emplace_back using perfect forwarding and variadic templates (you
//   // can use this in push then)
//   // 8. Add size() function
//   // 9. Any more suggestions ??
//   // 10. Why no shared_ptr ?? [Reason this]

//   // Constructor
//   lockfree_spsc_unbounded();

//   // Destructor
//   ~lockfree_spsc_unbounded();

//   // Copy constructor and assignment operator deleted
//   lockfree_spsc_unbounded(const lockfree_spsc_unbounded &) = delete;
//   lockfree_spsc_unbounded &operator=(const lockfree_spsc_unbounded &) = delete;

//   // Move constructor and assignment operator deleted
//   lockfree_spsc_unbounded(lockfree_spsc_unbounded &&) = delete;
//   lockfree_spsc_unbounded &operator=(lockfree_spsc_unbounded &&) = delete;

//   // Push function
//   void push(T value);

//   // Emplace function
//   template <typename... Args>
//   void emplace(Args &&...args);

//   // Wait and pop function
//   void wait_and_pop(T &ref);

//   // Try pop function
//   bool try_pop(T &ref);

//   // Empty function
//   bool empty();

//   // Peek function
//   bool peek(T &ref);

//   // Size function
//   size_t size();
// };
// } // namespace tsfqueue::__impl

// // Include the out-of-line template definitions
// #include "impl.hpp"

#endif