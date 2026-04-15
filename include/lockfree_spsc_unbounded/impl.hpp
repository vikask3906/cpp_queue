#ifndef LOCKFREE_SPSC_UNBOUNDED_IMPL
#define LOCKFREE_SPSC_UNBOUNDED_IMPL

// No #include "defs.hpp" needed — this file is always included from the
// bottom of defs.hpp, after the class definition is already visible.

namespace tsfqueue::__impl {

// Constructor: create a single stub node, head and tail both point to it
template <typename T>
lockfree_spsc_unbounded<T>::lockfree_spsc_unbounded() {
    node *stub = new node();
    stub->next.store(nullptr, std::memory_order_relaxed);
    head = stub;
    tail = stub;
}

// Destructor: walk the list and delete every node
template <typename T>
lockfree_spsc_unbounded<T>::~lockfree_spsc_unbounded() {
    while (head != nullptr) {
        node *temp = head;
        head = head->next.load(std::memory_order_relaxed);
        delete temp;
    }
}

// Producer-only: push a value into the queue
// Stores data into the current tail (stub), creates a new stub, publishes it
template <typename T>
void lockfree_spsc_unbounded<T>::push(T value) {
    node *new_node = new node();
    new_node->next.store(nullptr, std::memory_order_relaxed);

    // Store data in current tail (the stub node), then publish the new stub
    tail->data = std::move(value);
    tail->next.store(new_node, std::memory_order_release);  // publish to consumer
    tail = new_node;  // new_node becomes the new stub
}



// Consumer-only: try to pop a value (non-blocking)
// Returns false if queue is empty, true otherwise
template <typename T>
bool lockfree_spsc_unbounded<T>::try_pop(T &ref) {
    // If head->next is nullptr, queue is empty (head is the stub/sentinel)
    node *next_node = head->next.load(std::memory_order_acquire);
    if (next_node == nullptr) {
        return false;  // queue is empty
    }
    // Data was stored in head by the producer during push
    ref = std::move(head->data);
    node *old_head = head;
    head = next_node;  // advance head to next node
    delete old_head;   // free the consumed node
    return true;
}

// Consumer-only: bolcking pop (spins until data is available)
template <typename T>
void lockfree_spsc_unbounded<T>::wait_and_pop(T &ref) {
    size_t spin_count=0;
    while (!try_pop(ref)) {
        // Yield to avoid burning 100% CPU while spinning
        if (spin_count<100)
        spin_count++;
        else
        std::this_thread::yield();
    }
}

// Consumer-only: peek at the front element without removing it
template <typename T>
bool lockfree_spsc_unbounded<T>::peek(T &ref) {
    node *next_node = head->next.load(std::memory_order_acquire);
    if (next_node == nullptr) {
        return false;  // queue is empty
    }
    ref = head->data;  // copy from head, do NOT move (we're just peeking)
    return true;
}

// Check if queue is empty
template <typename T>
bool lockfree_spsc_unbounded<T>::empty() {
    return head->next.load(std::memory_order_acquire) == nullptr;
}


} // namespace tsfqueue::__impl

#endif

// 1. Add static asserts
// 2. Add emplace_back using perfect forwarding and variadic templates (you
// can use this in push then)
// 3. Add size() function
// 4. Any more suggestions ??



// Producer-only: emplace a value using perfect forwarding
// template <typename T>
// template <typename... Args>
// void lockfree_spsc_unbounded<T>::emplace(Args &&...args) {
//     node *new_node = new node();
//     new_node->next.store(nullptr, std::memory_order_relaxed);

//     tail->data = T(std::forward<Args>(args)...);
//     tail->next.store(new_node, std::memory_order_release);
//     tail = new_node;
// }

// Count elements in the queue
// WARNING: only accurate if called from one thread (consumer side)
// 

// template <typename T>
// size_t lockfree_spsc_unbounded<T>::size() {
//     size_t count = 0;
//     node *temp = head->next.load(std::memory_order_acquire);
//     while (temp != nullptr) {
//         count++;
//         temp = temp->next.load(std::memory_order_acquire);
//     }
//     return count;
// }