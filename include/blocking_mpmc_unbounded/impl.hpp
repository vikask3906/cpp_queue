#ifndef BLOCKING_MPMC_UNBOUNDED_IMPL
#define BLOCKING_MPMC_UNBOUNDED_IMPL

namespace tsfqueue::__impl {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
//
// Creates the sentinel/dummy node.  Both head and tail point to it.
// head owns it via unique_ptr; tail holds a raw non-owning pointer.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
blocking_mpmc_unbounded<T>::blocking_mpmc_unbounded()
    : head(std::make_unique<node>()), tail(head.get()) {}

// ─────────────────────────────────────────────────────────────────────────────
// get_tail  (private, called by consumer helpers)
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
typename blocking_mpmc_unbounded<T>::node *
blocking_mpmc_unbounded<T>::get_tail() {
    std::lock_guard<std::mutex> lock(tail_mutex);
    return tail;
}

// ─────────────────────────────────────────────────────────────────────────────
// wait_and_get  (private)
//
// Acquires head_mutex, waits on the condition variable until the queue is
// non-empty, then unlinks and returns the current head node.
//
// NOTE: get_tail() acquires tail_mutex internally.  We must not hold
// tail_mutex before calling get_tail() from inside the lambda, otherwise we
// would have a lock-ordering inversion.  head_mutex → tail_mutex is the
// correct order here; we never hold tail_mutex and then try to acquire
// head_mutex.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
std::unique_ptr<typename blocking_mpmc_unbounded<T>::node>
blocking_mpmc_unbounded<T>::wait_and_get() {
    std::unique_lock<std::mutex> lock(head_mutex);
    cond.wait(lock, [this]() { return head.get() != get_tail(); });

    std::unique_ptr<node> old_head = std::move(head);
    head = std::move(old_head->next);
    return old_head;
}

// ─────────────────────────────────────────────────────────────────────────────
// try_get  (private)
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
std::unique_ptr<typename blocking_mpmc_unbounded<T>::node>
blocking_mpmc_unbounded<T>::try_get() {
    std::unique_lock<std::mutex> lock(head_mutex);
    if (head.get() == get_tail()) {
        return nullptr;
    }
    std::unique_ptr<node> old_head = std::move(head);
    head = std::move(old_head->next);
    return old_head;
}

// ─────────────────────────────────────────────────────────────────────────────
// push
//
// Stores the value in the current dummy node, chains a new dummy, advances
// tail, then signals one waiting consumer.
//
// The tail_mutex protects: reading/writing tail, and writing tail->data and
// tail->next.  The consumer never touches tail directly — it compares head
// against get_tail() which acquires tail_mutex, so the full tail node is
// always observed consistently.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
void blocking_mpmc_unbounded<T>::push(T value) {
    std::shared_ptr<T> new_data = std::make_shared<T>(std::move(value));
    auto new_dummy = std::make_unique<node>();
    {
        std::lock_guard<std::mutex> lock(tail_mutex);
        tail->data = new_data;
        tail->next = std::move(new_dummy);
        tail = tail->next.get();
    }
    cond.notify_one();
}

// ─────────────────────────────────────────────────────────────────────────────
// emplace
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
template <typename... Args>
void blocking_mpmc_unbounded<T>::emplace(Args &&...args) {
    std::shared_ptr<T> new_data = std::make_shared<T>(std::forward<Args>(args)...);
    auto new_dummy = std::make_unique<node>();
    {
        std::lock_guard<std::mutex> lock(tail_mutex);
        tail->data = new_data;
        tail->next = std::move(new_dummy);
        tail = tail->next.get();
    }
    cond.notify_one();
}

// ─────────────────────────────────────────────────────────────────────────────
// wait_and_pop — reference version
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
void blocking_mpmc_unbounded<T>::wait_and_pop(T &ref) {
    std::unique_ptr<node> old_head = wait_and_get();
    ref = std::move(*(old_head->data));
}

// ─────────────────────────────────────────────────────────────────────────────
// wait_and_pop — shared_ptr version
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
std::shared_ptr<T> blocking_mpmc_unbounded<T>::wait_and_pop() {
    std::unique_ptr<node> old_head = wait_and_get();
    return old_head->data;
}

// ─────────────────────────────────────────────────────────────────────────────
// try_pop — reference version
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
bool blocking_mpmc_unbounded<T>::try_pop(T &ref) {
    std::unique_ptr<node> old_head = try_get();
    if (!old_head) {
        return false;
    }
    ref = std::move(*(old_head->data));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// try_pop — shared_ptr version
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
std::shared_ptr<T> blocking_mpmc_unbounded<T>::try_pop() {
    std::unique_ptr<node> old_head = try_get();
    if (!old_head) {
        return nullptr;
    }
    return old_head->data;
}

// ─────────────────────────────────────────────────────────────────────────────
// empty
//
// Acquires head_mutex first, then tail_mutex (via get_tail).
// This ordering must be consistent everywhere both locks are held.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
bool blocking_mpmc_unbounded<T>::empty() {
    std::lock_guard<std::mutex> lock(head_mutex);
    return head.get() == get_tail();
}

// ─────────────────────────────────────────────────────────────────────────────
// size
//
// FIX vs. original: the original traversed the list on every call, acquiring
// tail_mutex inside the loop on every node — O(n) lock acquisitions.
//
// This version simply walks the list once while holding head_mutex.
// get_tail() is called once to snapshot the tail pointer; we then walk
// until we reach that snapshot.  New nodes pushed after the snapshot are not
// counted — this is correct snapshot semantics for a concurrent container.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
size_t blocking_mpmc_unbounded<T>::size() {
    std::lock_guard<std::mutex> lock(head_mutex);
    node *const snap_tail = get_tail(); // one tail_mutex acquisition
    size_t count = 0;
    for (node *cur = head.get(); cur != snap_tail; cur = cur->next.get()) {
        ++count;
    }
    return count;
}

} // namespace tsfqueue::__impl

#endif // BLOCKING_MPMC_UNBOUNDED_IMPL




// #ifndef BLOCKING_MPMC_UNBOUNDED_IMPL
// #define BLOCKING_MPMC_UNBOUNDED_IMPL

// // No #include "defs.hpp" needed — this file is always included from the
// // bottom of defs.hpp, after the class definition is already visible.

// namespace tsfqueue::__impl {

// // Constructor
// template <typename T>
// blocking_mpmc_unbounded<T>::blocking_mpmc_unbounded()
//     : head(std::make_unique<node>()), tail(head.get()) {}

// // Private: get_tail
// template <typename T>
// typename blocking_mpmc_unbounded<T>::node *blocking_mpmc_unbounded<T>::get_tail() {
//     std::lock_guard<std::mutex> lock(tail_mutex);
//     return tail;
// }

// // Private: wait_and_get (blocking)
// template <typename T>
// std::unique_ptr<typename blocking_mpmc_unbounded<T>::node> blocking_mpmc_unbounded<T>::wait_and_get() {
//     std::unique_lock<std::mutex> lock(head_mutex);
//     cond.wait(lock, [this]() { return head.get() != get_tail(); });
//     std::unique_ptr<node> old_head = std::move(head);
//     head = std::move(old_head->next);
//     return old_head;
// }

// // Private: try_get (non-blocking)
// template <typename T>
// std::unique_ptr<typename blocking_mpmc_unbounded<T>::node> blocking_mpmc_unbounded<T>::try_get() {
//     std::unique_lock<std::mutex> lock(head_mutex);
//     if (head.get() == get_tail()) {
//         return nullptr;
//     }
//     std::unique_ptr<node> old_head = std::move(head);
//     head = std::move(old_head->next);
//     return old_head;
// }

// // Public: push
// template <typename T>
// void blocking_mpmc_unbounded<T>::push(T value) {
//     std::shared_ptr<T> new_data = std::make_shared<T>(std::move(value));
//     std::lock_guard<std::mutex> lock(tail_mutex);
//     tail->data = new_data;
//     tail->next = std::make_unique<node>();
//     tail = tail->next.get();
//     cond.notify_one();
// }

// // Public: emplace (construct T in-place using perfect forwarding)
// template <typename T>
// template <typename... Args>
// void blocking_mpmc_unbounded<T>::emplace(Args &&...args) {
//     std::shared_ptr<T> new_data = std::make_shared<T>(std::forward<Args>(args)...);
//     std::lock_guard<std::mutex> lock(tail_mutex);
//     tail->data = new_data;
//     tail->next = std::make_unique<node>();
//     tail = tail->next.get();
//     cond.notify_one();
// }

// // Public: wait_and_pop (reference version)
// template <typename T>
// void blocking_mpmc_unbounded<T>::wait_and_pop(T &ref) {
//     std::unique_ptr<node> old_head = wait_and_get();
//     ref = std::move(*(old_head->data));
// }

// // Public: wait_and_pop (shared_ptr version)
// template <typename T>
// std::shared_ptr<T> blocking_mpmc_unbounded<T>::wait_and_pop() {
//     std::unique_ptr<node> old_head = wait_and_get();
//     return old_head->data;
// }   

// // Public: try_pop (reference version)
// template <typename T>
// bool blocking_mpmc_unbounded<T>::try_pop(T &ref) {
//     std::unique_ptr<node> const old_head = try_get();
//     if (!old_head) {
//         return false;
//     }
//     ref = std::move(*(old_head->data));
//     return true;
// }

// // Public: try_pop (shared_ptr version)
// template <typename T>
// std::shared_ptr<T> blocking_mpmc_unbounded<T>::try_pop() {
//     std::unique_ptr<node> const old_head = try_get();
//     if (!old_head) {
//         return nullptr;
//     }
//     return old_head->data;
// }

// // Public: empty
// template <typename T>
// bool blocking_mpmc_unbounded<T>::empty() {
//     std::lock_guard<std::mutex> lock(head_mutex);
//     return head.get() == get_tail();
// }

// // Public: size
// template <typename T>
// size_t blocking_mpmc_unbounded<T>::size() {
//     std::lock_guard<std::mutex> lock(head_mutex);
//     size_t count = 0;
//     for (node *temp = head.get(); temp != get_tail(); temp = temp->next.get()) {
//         count++;
//     }
//     return count;
// }

// } // namespace tsfqueue::__impl

// #endif