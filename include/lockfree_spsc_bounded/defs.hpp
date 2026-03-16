#ifndef LOCKFREE_SPSC_BOUNDED_DEFS
#define LOCKFREE_SPSC_BOUNDED_DEFS

#include "utils.hpp"
#include <atomic>
#include <memory>
#include <type_traits>

namespace tsfqueue::__impl {
template <typename T, size_t Capacity> class lockfree_spsc_bounded {
  // For the implementation, we first take the size of the bounded queue from
  // user inside the templates so that we can do compile time memory allocation.
  // We have two atomic pointer, head and tail, tail for pushing the element and
  // head for popping. We also add check tail == head for empty which means one
  // redundant element during allocation. We keep head_cache and tail_cache as
  // cached copies to have a cache efficient code (discuss with me for details).
  // All the data members are cache aligned to prevent cache-line bouncing.
  // The user is provided with both set of functions : try_pop() and try_push()
  // for a wait-free code And wait_and_pop() and wait_and_push() for a lock-less
  // code but not wait-free variant. Thus, the user is given a choice to choose
  // among the preferred endpoints as per use case.
private:
  // Add the private members :
  // std::atomic<size_t> head;
  // std::atomic<size_t> tail;
  // size_t head_cache;
  // size_t tail_cache;
  // T arr[];
  // static constexpr size_t capacity;

  // Description of private members :
  // 1. std::atomic<size_t> head is the atomic head pointer
  // 2. std::atomic<size_t> tail is the atomic tail pointer
  // 3. size_t head_cache is the cached head pointer
  // 4. size_t tail_cache is the cached tail pointer
  // 5. T arr[] compile time allocated array
  // Cache align 1-5.
  // 6. static constexpr size_t capcity to store the capcity for operations in
  // functions Why static ?? Why constexpr ?? [Reason this]

public:
  // Public Member functions :
  // Add appropriate constructors and destructors -> Add here only
  // 1. void wait_and_push(value) : Busy wait until element is pushed
  // 2. bool try_push(value) : Try to push if not full else leave (returns false
  // if could not push else true)
  // 3. void wait_and_pop(value ref) : Busy wait until we have atmost 1 elt and
  // then pop it and store in reference
  // 4. bool try_pop(value ref) : Try to pop and return false if failed bool
  // 5. empty(void) : Checks if the queue is empty and return bool
  // 6. bool peek(value ref) : Peek the top of the queue.
  // Will work only in SPSC/MPSC why ?? [Reason this]
  // 7. Add static asserts
  // 8. Add emplace_back using perfect forwarding and variadic templates (you
  // can use this in push then)
  // 9. Add size() function
  // 10. Any more suggestions ??
  // 11. Why no shared_ptr ?? [Reason this]
};
} // namespace tsfqueue::__impl

#endif