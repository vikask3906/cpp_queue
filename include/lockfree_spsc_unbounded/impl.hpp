#ifndef LOCKFREE_SPSC_UNBOUNDED_IMPL
#define LOCKFREE_SPSC_UNBOUNDED_IMPL

namespace tsfqueue::__impl {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
//
// Allocates the initial stub node.  Both head and tail point to it.
// The stub has no data and next == nullptr, signalling "queue is empty".
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
lockfree_spsc_unbounded<T>::lockfree_spsc_unbounded() {
    node *stub = new node();
    stub->next.store(nullptr, std::memory_order_relaxed);
    head = stub;
    tail = stub;
}

// ─────────────────────────────────────────────────────────────────────────────
// Destructor
//
// Walks from head to the end and deletes every node, including the final stub.
// Only the consumer thread should call the destructor (or ensure no other
// thread is still accessing the queue).
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
lockfree_spsc_unbounded<T>::~lockfree_spsc_unbounded() {
    while (head != nullptr) {
        node *temp = head;
        head = head->next.load(std::memory_order_relaxed);
        delete temp;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// push  (producer-only)
//
// Conceptual invariant:
//   tail always points to the "stub" — a node with next == nullptr and
//   *no* valid data yet.  push writes data into the current stub, chains a
//   new empty stub, then advances tail to the new stub.
//
// Memory ordering:
//   new_node->next.store(relaxed) — only the producer touches the new node at
//     this point; no consumer can see it yet, so relaxed is sufficient.
//   tail->next.store(release) — this is the publication event.  The release
//     ensures that the preceding store to tail->data is visible to any thread
//     that subsequently does an acquire load on this same next pointer.
//
// The producer does NOT need to make tail itself atomic because tail is only
// ever read and written by the producer.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
void lockfree_spsc_unbounded<T>::push(T value) {
    node *new_stub = new node();
    new_stub->next.store(nullptr, std::memory_order_relaxed);

    tail->data = std::move(value);                          // write data into current stub
    tail->next.store(new_stub, std::memory_order_release);  // publish to consumer
    tail = new_stub;                                         // advance producer's tail
}

// ─────────────────────────────────────────────────────────────────────────────
// emplace  (producer-only)
//
// Constructs T in-place from forwarded args instead of moving an already-
// constructed value.  std::construct_at (C++20) avoids an extra move vs.
// T(std::forward<Args>(args)...) which constructs a temporary first.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
template <typename... Args>
void lockfree_spsc_unbounded<T>::emplace(Args &&...args) {
    node *new_stub = new node();
    new_stub->next.store(nullptr, std::memory_order_relaxed);

    std::construct_at(&tail->data, std::forward<Args>(args)...);
    tail->next.store(new_stub, std::memory_order_release);
    tail = new_stub;
}

// ─────────────────────────────────────────────────────────────────────────────
// try_pop  (consumer-only)
//
// Memory ordering:
//   head->next.load(acquire) — pairs with push's tail->next.store(release),
//     making tail->data visible to the consumer.
//   (head itself is never atomic — only the consumer modifies it)
//
// Data flow:
//   head     → sentinel node (data was written here by push; we read it now)
//   next_node → the node that was the stub when push ran; it is the *new*
//               sentinel after we advance head.
//
// We read data from head (the old stub-turned-data-node), then make next_node
// the new sentinel by advancing head, then delete the old sentinel.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
bool lockfree_spsc_unbounded<T>::try_pop(T &ref) {
    node *next_node = head->next.load(std::memory_order_acquire);
    if (next_node == nullptr) {
        return false; // queue is empty
    }

    ref = std::move(head->data); // read value from current sentinel
    node *old_head = head;
    head = next_node;            // advance sentinel
    delete old_head;             // free the consumed node
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// wait_and_pop  (consumer-only)
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
void lockfree_spsc_unbounded<T>::wait_and_pop(T &ref) {
    int spin_count = 0;
    while (!try_pop(ref)) {
        if (spin_count++ < 100) {
            // Busy-spin: avoids syscall overhead for very short waits.
        } else {
            std::this_thread::yield();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// peek  (consumer-only)
//
// Reads the front value without removing it.
// Safe in SPSC: the consumer is the only one that can advance head, so the
// element at head->data cannot disappear between this call and the caller's
// next action.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
bool lockfree_spsc_unbounded<T>::peek(T &ref) const {
    node *next_node = head->next.load(std::memory_order_acquire);
    if (next_node == nullptr) {
        return false;
    }
    ref = head->data; // copy, not move — we are only peeking
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// empty
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
bool lockfree_spsc_unbounded<T>::empty() const {
    return head->next.load(std::memory_order_acquire) == nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// size
//
// Walks the list from head to the first nullptr next pointer.  This is O(n)
// and is a snapshot — elements may be pushed or popped concurrently.
//
// Safety: the acquire loads on each node's next ensure we follow a consistent
// chain.  We will never dereference a pointer the producer hasn't finished
// publishing because we stop when next == nullptr.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
size_t lockfree_spsc_unbounded<T>::size() const {
    size_t count = 0;
    // head->next is the first real data node (head itself is the sentinel).
    node *cur = head->next.load(std::memory_order_acquire);
    while (cur != nullptr) {
        ++count;
        cur = cur->next.load(std::memory_order_acquire);
    }
    return count;
}

} // namespace tsfqueue::__impl

#endif // LOCKFREE_SPSC_UNBOUNDED_IMPL




    // #ifndef LOCKFREE_SPSC_UNBOUNDED_IMPL
    // #define LOCKFREE_SPSC_UNBOUNDED_IMPL

    // // No #include "defs.hpp" needed — this file is always included from the
    // // bottom of defs.hpp, after the class definition is already visible.

    // namespace tsfqueue::__impl {

    // // Constructor: create a single stub node, head and tail both point to it
    // template <typename T>
    // lockfree_spsc_unbounded<T>::lockfree_spsc_unbounded() {
    //     node *stub = new node();
    //     stub->next.store(nullptr, std::memory_order_relaxed);
    //     head = stub;
    //     tail = stub;
    // }

    // // Destructor: walk the list and delete every node
    // template <typename T>
    // lockfree_spsc_unbounded<T>::~lockfree_spsc_unbounded() {
    //     while (head != nullptr) {
    //         node *temp = head;
    //         head = head->next.load(std::memory_order_relaxed);
    //         delete temp;
    //     }
    // }

    // // Producer-only: push a value into the queue
    // // Stores data into the current tail (stub), creates a new stub, publishes it
    // template <typename T>
    // void lockfree_spsc_unbounded<T>::push(T value) {
    //     node *new_node = new node();
    //     new_node->next.store(nullptr, std::memory_order_relaxed);

    //     // Store data in current tail (the stub node), then publish the new stub
    //     tail->data = std::move(value);
    //     tail->next.store(new_node, std::memory_order_release);  // publish to consumer
    //     tail = new_node;  // new_node becomes the new stub
    // }



    // // Consumer-only: try to pop a value (non-blocking)
    // // Returns false if queue is empty, true otherwise
    // template <typename T>
    // bool lockfree_spsc_unbounded<T>::try_pop(T &ref) {
    //     // If head->next is nullptr, queue is empty (head is the stub/sentinel)
    //     node *next_node = head->next.load(std::memory_order_acquire);
    //     if (next_node == nullptr) {
    //         return false;  // queue is empty
    //     }
    //     // Data was stored in head by the producer during push
    //     ref = std::move(head->data);
    //     node *old_head = head;
    //     head = next_node;  // advance head to next node
    //     delete old_head;   // free the consumed node
    //     return true;
    // }

    // // Consumer-only: bolcking pop (spins until data is available)
    // template <typename T>
    // void lockfree_spsc_unbounded<T>::wait_and_pop(T &ref) {
    //     size_t spin_count=0;
    //     while (!try_pop(ref)) {
    //         // Yield to avoid burning 100% CPU while spinning
    //         if (spin_count<100)
    //         spin_count++;
    //         else
    //         std::this_thread::yield();
    //     }
    // }

    // // Consumer-only: peek at the front element without removing it
    // template <typename T>
    // bool lockfree_spsc_unbounded<T>::peek(T &ref) {
    //     node *next_node = head->next.load(std::memory_order_acquire);
    //     if (next_node == nullptr) {
    //         return false;  // queue is empty
    //     }
    //     ref = head->data;  // copy from head, do NOT move (we're just peeking)
    //     return true;
    // }

    // // Check if queue is empty
    // template <typename T>
    // bool lockfree_spsc_unbounded<T>::empty() {
    //     return head->next.load(std::memory_order_acquire) == nullptr;
    // }

    // // Producer-only: emplace a value using perfect forwarding
    // template <typename T>
    // template <typename... Args>
    // void lockfree_spsc_unbounded<T>::emplace(Args &&...args) {
    //     node *new_node = new node();
    //     new_node->next.store(nullptr, std::memory_order_relaxed);

    //     tail->data = T(std::forward<Args>(args)...);
    //     tail->next.store(new_node, std::memory_order_release);
    //     tail = new_node;
    // }

    // // Count elements in the queue
    // // WARNING: only a snapshot, walks the linked list
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

    // } // namespace tsfqueue::__impl

    // #endif