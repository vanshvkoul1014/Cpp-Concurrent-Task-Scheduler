#include "thread_pool.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <thread>
#include <vector>

// CPU-bound workload: sum of sqrt over a range. Deterministic, no I/O.
static double cpu_work(int n) {
    double acc = 0.0;
    for (int i = 0; i < n; ++i) {
        acc += std::sqrt(static_cast<double>(i) + 1.0);
    }
    return acc;
}

using Clock = std::chrono::high_resolution_clock;

static double elapsed_ms(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

int main() {
    const int NUM_TASKS = 2000;
    const int WORK_N = 50000;  // iterations of sqrt per task
    const unsigned hw_threads = std::max(1u, std::thread::hardware_concurrency());

    printf("Detected hardware_concurrency: %u\n", hw_threads);
    printf("Workload: %d tasks x %d sqrt-iterations each\n\n", NUM_TASKS, WORK_N);

    // --- 1. Sequential baseline ---
    {
        auto start = Clock::now();
        double sink = 0.0;
        for (int i = 0; i < NUM_TASKS; ++i) {
            sink += cpu_work(WORK_N);
        }
        auto end = Clock::now();
        double ms = elapsed_ms(start, end);
        printf("Sequential:       %8.2f ms   (%.0f tasks/sec)  [checksum=%.2f]\n",
               ms, NUM_TASKS / (ms / 1000.0), sink);
    }

    // --- 2. Thread-per-task ---
    {
        auto start = Clock::now();
        std::vector<std::future<double>> futures;
        futures.reserve(NUM_TASKS);
        std::vector<std::thread> threads;
        threads.reserve(NUM_TASKS);
        std::vector<double> results(NUM_TASKS);

        for (int i = 0; i < NUM_TASKS; ++i) {
            threads.emplace_back([i, &results, WORK_N] {
                results[i] = cpu_work(WORK_N);
            });
        }
        for (auto& t : threads) t.join();

        auto end = Clock::now();
        double ms = elapsed_ms(start, end);
        double sink = std::accumulate(results.begin(), results.end(), 0.0);
        printf("Thread-per-task:  %8.2f ms   (%.0f tasks/sec)  [checksum=%.2f]\n",
               ms, NUM_TASKS / (ms / 1000.0), sink);
    }

    // --- 3. Pooled execution (this scheduler) ---
    {
        auto start = Clock::now();
        ThreadPool pool(hw_threads);
        std::vector<std::future<double>> futures;
        futures.reserve(NUM_TASKS);
        for (int i = 0; i < NUM_TASKS; ++i) {
            futures.push_back(pool.submit(TaskPriority::Normal, cpu_work, WORK_N));
        }
        double sink = 0.0;
        for (auto& f : futures) sink += f.get();
        pool.shutdown();

        auto end = Clock::now();
        double ms = elapsed_ms(start, end);
        printf("Pooled (%2u thr):  %8.2f ms   (%.0f tasks/sec)  [checksum=%.2f]\n",
               hw_threads, ms, NUM_TASKS / (ms / 1000.0), sink);
    }

    return 0;
}
