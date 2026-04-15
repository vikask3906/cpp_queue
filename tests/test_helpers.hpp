#pragma once
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <numeric>

// ─────────────────────────────────────────────────────────────────────────────
// MATHEMATICAL BASIS — Multiset equality (Linearizability verification)
//
//   A correct concurrent queue must satisfy: the multiset of all consumed
//   items equals the multiset of all produced items. If we produce {0..N-1},
//   then after sorting the consumed vector, we must see exactly {0..N-1}.
//
//   Why multiset and not just count?
//     A simple count check passes even if every item is duplicated and a
//     different set is lost (net count stays the same). The sorted comparison
//     catches both errors independently:
//       - duplicates: consumed[i] == consumed[i-1]
//       - missing:    expected_count - unique_items_seen > 0
// ─────────────────────────────────────────────────────────────────────────────
struct AuditResult {
    int duplicates;
    int missing;
    bool ok() const { return duplicates == 0 && missing == 0; }
};

inline AuditResult audit_items(std::vector<int> consumed, int expected_count) {
    std::sort(consumed.begin(), consumed.end());
    int dups = 0;
    for (size_t i = 1; i < consumed.size(); ++i)
        if (consumed[i] == consumed[i - 1]) ++dups;

    int unique_seen = static_cast<int>(consumed.size()) - dups;
    int missing = expected_count - unique_seen;
    return {dups, missing};
}

// ─────────────────────────────────────────────────────────────────────────────
// MATHEMATICAL BASIS — Watchdog (Progress / Liveness verification)
//
//   For blocking queues, we need to verify the liveness property: every
//   waiting thread must eventually make progress. A deadlocked queue violates
//   this — the atomic counter stops advancing.
//
//   The watchdog samples a shared atomic counter at fixed intervals. If it
//   has not advanced between two samples, the queue is stalled and we fire
//   a GTest failure, preventing infinite hangs.
// ─────────────────────────────────────────────────────────────────────────────
struct Watchdog {
    std::atomic<int>& counter;
    std::thread watcher;
    std::atomic<bool> stop{false};

    Watchdog(std::atomic<int>& c, int timeout_ms)
        : counter(c)
    {
        watcher = std::thread([this, timeout_ms] {
            int last = counter.load();
            while (!stop.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
                int now = counter.load();
                if (!stop.load())
                    EXPECT_GT(now, last) << "Queue appears deadlocked — "
                        "no progress in " << timeout_ms << "ms";
                last = now;
            }
        });
    }
    ~Watchdog() {
        stop.store(true);
        if (watcher.joinable()) watcher.join();
    }
};
