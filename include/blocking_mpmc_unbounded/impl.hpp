#ifndef BLOCKING_MPMC_UNBOUNDED_IMPL
#define BLOCKING_MPMC_UNBOUNDED_IMPL

// No #include "defs.hpp" needed — this file is always included from the
// bottom of defs.hpp, after the class definition is already visible.

namespace tsfqueue::__impl {

// Constructor
template <typename T>
blocking_mpmc_unbounded<T>::blocking_mpmc_unbounded()
    : head(std::make_unique<node>()), tail(head.get()) {}

// Private: get_tail
template <typename T>
typename blocking_mpmc_unbounded<T>::node *blocking_mpmc_unbounded<T>::get_tail() {
    std::lock_guard<std::mutex> lock(tail_mutex);
    return tail;
}

// Private: wait_and_get (blocking)
template <typename T>
std::unique_ptr<typename blocking_mpmc_unbounded<T>::node> blocking_mpmc_unbounded<T>::wait_and_get() {
    std::unique_lock<std::mutex> lock(head_mutex);
    cond.wait(lock, [this]() { return head.get() != get_tail(); });
    std::unique_ptr<node> old_head = std::move(head);
    head = std::move(old_head->next);
    return old_head;
}

// Private: try_get (non-blocking)
template <typename T>
std::unique_ptr<typename blocking_mpmc_unbounded<T>::node> blocking_mpmc_unbounded<T>::try_get() {
    std::unique_lock<std::mutex> lock(head_mutex);
    if (head.get() == get_tail()) {
        return nullptr;
    }
    std::unique_ptr<node> old_head = std::move(head);
    head = std::move(old_head->next);
    return old_head;
}

// Public: push
template <typename T>
void blocking_mpmc_unbounded<T>::push(T value) {
    std::shared_ptr<T> new_data = std::make_shared<T>(std::move(value));
    std::lock_guard<std::mutex> lock(tail_mutex);
    tail->data = new_data;
    tail->next = std::make_unique<node>();
    tail = tail->next.get();
    cond.notify_one();
}

// Public: emplace (construct T in-place using perfect forwarding)
template <typename T>
template <typename... Args>
void blocking_mpmc_unbounded<T>::emplace(Args &&...args) {
    std::shared_ptr<T> new_data = std::make_shared<T>(std::forward<Args>(args)...);
    std::lock_guard<std::mutex> lock(tail_mutex);
    tail->data = new_data;
    tail->next = std::make_unique<node>();
    tail = tail->next.get();
    cond.notify_one();
}

// Public: wait_and_pop (reference version)
template <typename T>
void blocking_mpmc_unbounded<T>::wait_and_pop(T &ref) {
    std::unique_ptr<node> old_head = wait_and_get();
    ref = std::move(*(old_head->data));
}

// Public: wait_and_pop (shared_ptr version)
template <typename T>
std::shared_ptr<T> blocking_mpmc_unbounded<T>::wait_and_pop() {
    std::unique_ptr<node> old_head = wait_and_get();
    return old_head->data;
}   

// Public: try_pop (reference version)
template <typename T>
bool blocking_mpmc_unbounded<T>::try_pop(T &ref) {
    std::unique_ptr<node> const old_head = try_get();
    if (!old_head) {
        return false;
    }
    ref = std::move(*(old_head->data));
    return true;
}

// Public: try_pop (shared_ptr version)
template <typename T>
std::shared_ptr<T> blocking_mpmc_unbounded<T>::try_pop() {
    std::unique_ptr<node> const old_head = try_get();
    if (!old_head) {
        return nullptr;
    }
    return old_head->data;
}

// Public: empty
template <typename T>
bool blocking_mpmc_unbounded<T>::empty() {
    std::lock_guard<std::mutex> lock(head_mutex);
    return head.get() == get_tail();
}

// Public: size
template <typename T>
size_t blocking_mpmc_unbounded<T>::size() {
    std::lock_guard<std::mutex> lock(head_mutex);
    size_t count = 0;
    for (node *temp = head.get(); temp != get_tail(); temp = temp->next.get()) {
        count++;
    }
    return count;
}

} // namespace tsfqueue::__impl

#endif