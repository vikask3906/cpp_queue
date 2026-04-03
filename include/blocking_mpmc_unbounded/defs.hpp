#ifndef BLOCKING_MPMC_UNBOUNDED_DEFS
#define BLOCKING_MPMC_UNBOUNDED_DEFS

#include "utils.hpp"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <type_traits>

namespace tsfqueue::__impl {
template <typename T> class blocking_mpmc_unbounded {
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
  using node = tsfqueue::__utils::Node<T>;

  // Private members
  std::mutex head_mutex;
  std::unique_ptr<node> head;
  std::mutex tail_mutex;
  node *tail;
  std::condition_variable cond;

  // Private helper functions (defined in impl.hpp)
  node *get_tail();
  std::unique_ptr<node> wait_and_get();
  std::unique_ptr<node> try_get();

public:
  // Constructor
  blocking_mpmc_unbounded();

  // No copying allowed (two queues can't share the same linked list)
  blocking_mpmc_unbounded(const blocking_mpmc_unbounded &) = delete;
  blocking_mpmc_unbounded &operator=(const blocking_mpmc_unbounded &) = delete;

  // Public member functions (defined in impl.hpp)
  void push(T value);

  template <typename... Args>
  void emplace(Args &&...args);

  void wait_and_pop(T &ref);
  std::shared_ptr<T> wait_and_pop();
  bool try_pop(T &ref);
  std::shared_ptr<T> try_pop();
  bool empty();
  size_t size();
};
} // namespace tsfqueue::__impl

// Include the out-of-line template definitions
#include "impl.hpp"

#endif