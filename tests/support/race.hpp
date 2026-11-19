#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

// Runs fn(0..count-1) on count threads released together by a start barrier.
inline void race_threads(int count, const std::function<void(int)> &fn)
{
    std::atomic<int> arrived{0};
    const auto barrier = [&]
    {
        arrived.fetch_add(1);
        while (arrived.load() < count + 1)
            std::this_thread::yield();
    };
    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
        threads.emplace_back(
            [&, i]
            {
                barrier();
                fn(i);
            });
    barrier();
    for (std::thread &thread : threads)
        thread.join();
}
