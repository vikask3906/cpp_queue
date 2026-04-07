#ifndef LOCKFREE_SPSC_UNBOUNDED_IMPL
#define LOCKFREE_SPSC_UNBOUNDED_IMPL

// No #include "defs.hpp" needed — this file is always included from the
// bottom of defs.hpp, after the class definition is already visible.

template <typename T>
using queue = tsfqueue::__impl::lockfree_spsc_unbounded<T>;

// Constructor: create a single stub node, head and tail both point to it
template <typename T>
queue<T>::lockfree_spsc_unbounded() {
    node *stub = new node();
    stub->next.store(nullptr, std::memory_order_relaxed);
    head = stub;
    tail = stub;
}

// Destructor: walk the list and delete every node
template <typename T>
queue<T>::~lockfree_spsc_unbounded() {
    while (head != nullptr) {
        node *temp = head;
        head = head->next.load(std::memory_order_relaxed);
        delete temp;
    }
}

// Producer-only: push a value into the queue
// Stores data into the current tail (stub), creates a new stub, publishes it
template <typename T>
void queue<T>::push(T value) {
    node *new_node = new node();
    new_node->next.store(nullptr, std::memory_order_relaxed);

    // Store data in current tail (the stub node), then publish the new stub
    tail->data = std::move(value);
    tail->next.store(new_node, std::memory_order_release);  // publish to consumer
    tail = new_node;  // new_node becomes the new stub
}

// Producer-only: emplace a value using perfect forwarding
template <typename T>
template <typename... Args>
void queue<T>::emplace(Args &&...args) {
    node *new_node = new node();
    new_node->next.store(nullptr, std::memory_order_relaxed);

    tail->data = T(std::forward<Args>(args)...);
    tail->next.store(new_node, std::memory_order_release);
    tail = new_node;
}

// Consumer-only: try to pop a value (non-blocking)
// Returns false if queue is empty, true otherwise
template <typename T>
bool_queue<T>::try_pop(T & ref)
{}
bool queue<T>::try_pop(T &ref) {
    // head is always the stub node. The real data is in head->next.
    node *next_node = head->next.load(std::memory_order_acquire);
    if (next_node == nullptr) {
        return false;  // queue is empty
    }
    // Data lives in the next node (because of stub pattern)
    ref = std::move(next_node->data);
    node *old_head = head;
    head = next_node;  // advance head past the old stub
    delete old_head;   // free the old stub
    return true;
}

// Consumer-only: blocking pop (spins until data is available)
template <typename T>
void queue<T>::wait_and_pop(T &ref) {
    while (!try_pop(ref)) {
        // Yield to avoid burning 100% CPU while spinning
        std::this_thread::yield();
    }
}

// Consumer-only: peek at the front element without removing it
template <typename T>
bool queue<T>::peek(T &ref) {
    node *next_node = head->next.load(std::memory_order_acquire);
    if (next_node == nullptr) {
        return false;  // queue is empty
    }
    ref = next_node->data;  // copy, do NOT move (we're just peeking)
    return true;
}

// Check if queue is empty
template <typename T>
bool queue<T>::empty() {
    return head->next.load(std::memory_order_acquire) == nullptr;
}

// Count elements in the queue
// WARNING: only accurate if called from one thread (consumer side)
template <typename T>
size_t queue<T>::size() {
    size_t count = 0;
    node *temp = head->next.load(std::memory_order_acquire);
    while (temp != nullptr) {
        count++;
        temp = temp->next.load(std::memory_order_acquire);
    }
    return count;
}       

#endif

// 1. Add static asserts
// 2. Add emplace_back using perfect forwarding and variadic templates (you
// can use this in push then)
// 3. Add size() function
// 4. Any more suggestions ??