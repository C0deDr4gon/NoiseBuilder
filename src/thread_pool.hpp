#pragma once
#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

// Run fn(i) for i in [0, n) on up to `threads` threads (0 = all cores).
// Jobs must be independent. Result same no matter thread count.
template <class Fn>
void parallelFor(int n, int threads, Fn fn) {
    if (threads <= 0) threads = (int)std::max(1u, std::thread::hardware_concurrency());
    threads = std::min(threads, n);
    if (threads <= 1) {
        for (int i = 0; i < n; ++i) fn(i);
        return;
    }
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([&] {
            for (int i; (i = next.fetch_add(1)) < n;) fn(i);
        });
    for (auto& th : pool) th.join();
}
