#ifndef LOCKFREE_SPSC_BOUNDED_IMPL_CT
#define LOCKFREE_SPSC_BOUNDED_IMPL_CT

#include "defs.hpp"

template <typename T, size_t Capacity>
using queue = tsfqueue::__impl::lockfree_spsc_bounded<T, Capacity>;

template <typename T, size_t Capacity>
void queue<T, Capacity>::wait_and_push(T value) {}

template <typename T, size_t Capacity>
bool queue<T, Capacity>::try_push(T value) {}

template <typename T, size_t Capacity>
bool queue<T, Capacity>::try_pop(T &value) {}

template <typename T, size_t Capacity>
void queue<T, Capacity>::wait_and_pop(T &value) {}

template <typename T, size_t Capacity>
bool queue<T, Capacity>::peek(T &value) {}

template <typename T, size_t Capacity> bool queue<T, Capacity>::empty() {}

#endif

// 1. Add static asserts
// 2. Add emplace_back using perfect forwarding and variadic templates (you
// can use this in push then)
// 3. Add size() function
// 4. Any more suggestions ??