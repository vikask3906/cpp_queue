#ifndef BLOCKING_MPMC_UNBOUNDED_DEFS
#define BLOCKING_MPMC_UNBOUNDED_DEFS

#include "utils.hpp"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <type_traits>

namespace tsfqueue::__impl {

template <typename T>
class blocking_mpmc_unbounded {
    // Linked-list based unbounded MPMC queue using a mutex-per-end design.
    //
    // Design notes:
    // - Two independent mutexes: head_mutex (consumers) and tail_mutex
    //   (producers).  This allows one producer and one consumer to proceed
    //   concurrently without contention — unlike a single global mutex.
    // - A permanent dummy/sentinel node is always present.  tail always points
    //   to this dummy; push fills the dummy and chains a new one.  head always
    //   points to the node whose *next* holds the next value to pop.  This
    //   prevents head and tail from ever touching the same node simultaneously
    //   (except when empty, which is handled by the empty check).
    // - std::shared_ptr<T> is used for data so that the two wait_and_pop /
    //   try_pop overloads can share ownership of the value without copying it.
    // - Moving the queue is deleted because the raw tail pointer would be left
    //   dangling after moving the unique_ptr chain.

    static_assert(std::is_move_constructible_v<T>,
                  "T must be move constructible");

private:
    using node = tsfqueue::__utils::Node<T>;

    // head_mutex guards head.  tail_mutex guards tail.
    // Always acquire head_mutex before tail_mutex if you need both (see
    // empty() and size()) to prevent deadlock.
    std::mutex             head_mutex;
    std::unique_ptr<node>  head;

    std::mutex             tail_mutex;
    node                  *tail;          // raw, non-owning pointer to the dummy

    std::condition_variable cond;         // signalled by push; waited on by wait_and_pop

    // ── Private helpers ────────────────────────────────────────────────────
    // get_tail: acquires tail_mutex and returns the current tail pointer.
    // Used by consumer-side helpers to safely compare against head.
    node *get_tail();

    // wait_and_get: blocks until the queue is non-empty, then unlinks and
    // returns the head node (which holds the next value).
    std::unique_ptr<node> wait_and_get();

    // try_get: non-blocking variant; returns nullptr if the queue is empty.
    std::unique_ptr<node> try_get();

public:
    // ── Constructor / destructor ───────────────────────────────────────────
    blocking_mpmc_unbounded();

    // Default destructor is fine — unique_ptr<node> chain is cleaned up
    // automatically when head goes out of scope.
    ~blocking_mpmc_unbounded() = default;

    // Copying is deleted: two queues cannot share the same linked list.
    blocking_mpmc_unbounded(const blocking_mpmc_unbounded &) = delete;
    blocking_mpmc_unbounded &operator=(const blocking_mpmc_unbounded &) = delete;

    // Moving is deleted: the raw tail pointer would alias into the moved-from
    // unique_ptr chain, which becomes invalid after the move.
    blocking_mpmc_unbounded(blocking_mpmc_unbounded &&) = delete;
    blocking_mpmc_unbounded &operator=(blocking_mpmc_unbounded &&) = delete;

    // ── Producer API ──────────────────────────────────────────────────────
    void push(T value);

    template <typename... Args>
    void emplace(Args &&...args);

    // ── Consumer API ──────────────────────────────────────────────────────
    // Two flavours of each pop:
    //   (a) reference version  — value is moved into the caller's variable.
    //   (b) shared_ptr version — returns shared ownership; useful when the
    //       caller needs to share the value with other threads without copying.
    void             wait_and_pop(T &ref);
    std::shared_ptr<T> wait_and_pop();

    bool             try_pop(T &ref);
    std::shared_ptr<T> try_pop();

    // ── Query API ─────────────────────────────────────────────────────────
    bool   empty();
    size_t size();
};

} // namespace tsfqueue::__impl

#include "blocking_mpmc_unbounded_impl.hpp"

#endif // BLOCKING_MPMC_UNBOUNDED_DEFS



// #ifndef BLOCKING_MPMC_UNBOUNDED_DEFS
// #define BLOCKING_MPMC_UNBOUNDED_DEFS

// #include "utils.hpp"
// #include <condition_variable>
// #include <memory>
// #include <mutex>
// #include <type_traits>

// namespace tsfqueue::__impl {
// template <typename T> class blocking_mpmc_unbounded {
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
//   using node = tsfqueue::__utils::Node<T>;

//   // Private members
//   std::mutex head_mutex;
//   std::unique_ptr<node> head;
//   std::mutex tail_mutex;
//   node *tail;
//   std::condition_variable cond;

//   // Private helper functions (defined in impl.hpp)
//   node *get_tail();
//   std::unique_ptr<node> wait_and_get();
//   std::unique_ptr<node> try_get();

// public:
//   // Constructor
//   blocking_mpmc_unbounded();

//   // No copying allowed (two queues can't share the same linked list)
//   blocking_mpmc_unbounded(const blocking_mpmc_unbounded &) = delete;
//   blocking_mpmc_unbounded &operator=(const blocking_mpmc_unbounded &) = delete;

//   // Public member functions (defined in impl.hpp)
//   void push(T value);

//   template <typename... Args>
//   void emplace(Args &&...args);

//   void wait_and_pop(T &ref);
//   std::shared_ptr<T> wait_and_pop();
//   bool try_pop(T &ref);
//   std::shared_ptr<T> try_pop();
//   bool empty();
//   size_t size();
// };
// } // namespace tsfqueue::__impl

// // Include the out-of-line template definitions
// #include "impl.hpp"

// #endif