#ifndef LOCKFREE_SPSC_UNBOUNDED_DEFS
#define LOCKFREE_SPSC_UNBOUNDED_DEFS

#include "utils.hpp"
#include <atomic>
#include <memory>
#include <thread>
#include <type_traits>

namespace tsfqueue::__impl {
template <typename T> class lockfree_spsc_unbounded {
  // Works exactly same as the blocking_mpmc_unbounded queue (see this once)
  // with tail pointer pointing to stub node and your head pointer updates as
  // per the pushes. See the Lockless_Node in utils to understand the working.
  // Note that the next pointers are atomic there. Why ?? [Reason this]
  // Also the head and tail members are cache-aligned. Why ?? [Reason this] (ask
  // me for details)

  // [Copy of blocking_mpmc_unbounded]
  // For the implementation, we start with a stub node and both head and tail
  // are initialized to it. When we push, we make a new stub node, move the data
  // into the current tail and then change the tail to the new stub. We have two
  // methods : wait_and_pop() which waits on the queue and returns element &
  // try_pop() which returns an element if queue is not empty otherwise returns
  // some neutral element OR a false boolean whichever is applicable. Pop works
  // by returning the data stored in head node and replacing head to its next
  // node. We handle the empty queue gracefully as per the pop type.

  static_assert(std::is_move_constructible_v<T>,
                "T must be move constructible");

private:
  using node = tsfqueue::__utils::Lockless_Node<T>;

  // Cache-aligned to prevent false sharing between producer and consumer
  alignas(tsfq::__impl::cache_line_size) node *head;
  alignas(tsfq::__impl::cache_line_size) node *tail;

  // Add the private members :
  // 1. node* head;
  // 2. node* tail;

  // Description of priavte members :
  // 1. node* head -> Pointer to the head node
  // 2. node* tail -> Pointer to tail node
  // 3. Cache align 1-2

public:
  // Public member functions :
  // Add relevant constructors and destructors -> Add these here only
  // 1. void push(value) : Pushes the value inside the queue, copies the value
  // 2. void wait_and_pop(value ref) : Blocking wait on queue, returns value in
  // the reference passed as parameter
  // 3. bool try_pop(value ref) : Returns true and
  // gives the value in reference passed, false otherwise
  // 4. bool empty() : Returns
  // whether the queue is empty or not at that instant
  // 5. bool peek(value ref) : Returns the front/top element of queue in ref (false if empty queue)
  // 6. Add static asserts
  // 7. Add emplace_back using perfect forwarding and variadic templates (you
  // can use this in push then)
  // 8. Add size() function
  // 9. Any more suggestions ??
  // 10. Why no shared_ptr ?? [Reason this]

  // Constructor
  lockfree_spsc_unbounded();

  // Destructor
  ~lockfree_spsc_unbounded();

  // Copy constructor and assignment operator deleted
  lockfree_spsc_unbounded(const lockfree_spsc_unbounded &) = delete;
  lockfree_spsc_unbounded &operator=(const lockfree_spsc_unbounded &) = delete;

  // Move constructor and assignment operator deleted
  lockfree_spsc_unbounded(lockfree_spsc_unbounded &&) = delete;
  lockfree_spsc_unbounded &operator=(lockfree_spsc_unbounded &&) = delete;

  // Push function
  void push(T value);

  // Emplace function
  template <typename... Args>
  void emplace(Args &&...args);

  // Wait and pop function
  void wait_and_pop(T &ref);

  // Try pop function
  bool try_pop(T &ref);

  // Empty function
  bool empty() const;

  // Peek function
  bool peek(T &ref) const;

  // Size function
  size_t size() const;
};
} // namespace tsfqueue::__impl

// Include the out-of-line template definitions
#include "impl.hpp"

#endif