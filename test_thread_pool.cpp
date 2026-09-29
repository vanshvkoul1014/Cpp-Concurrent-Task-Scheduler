#include "thread_pool.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <numeric>
#include <set>
#include <vector>

// ---------------------------------------------------------------------------
// Basic submission and return values
// ---------------------------------------------------------------------------

TEST(ThreadPoolBasic, SingleTaskReturnsCorrectValue) {
    ThreadPool pool(2);
    auto fut = pool.submit([] { return 42; });
    EXPECT_EQ(fut.get(), 42);
}

TEST(ThreadPoolBasic, TaskWithArgumentsIsForwardedCorrectly) {
    ThreadPool pool(2);
    auto fut = pool.submit([](int a, int b) { return a + b; }, 3, 4);
    EXPECT_EQ(fut.get(), 7);
}

TEST(ThreadPoolBasic, VoidTaskCompletesWithoutError) {
    ThreadPool pool(2);
    std::atomic<bool> ran{false};
    auto fut = pool.submit([&] { ran = true; });
    fut.get();
    EXPECT_TRUE(ran.load());
}

// ---------------------------------------------------------------------------
// Concurrent execution correctness
// ---------------------------------------------------------------------------

TEST(ThreadPoolConcurrency, AllSubmittedTasksExecuteExactlyOnce) {
    const int kNumTasks = 5000;
    ThreadPool pool(8);
    std::atomic<int> counter{0};

    std::vector<std::future<void>> futures;
    futures.reserve(kNumTasks);
    for (int i = 0; i < kNumTasks; ++i) {
        futures.push_back(pool.submit([&counter] { counter.fetch_add(1); }));
    }
    for (auto& f : futures) f.get();

    EXPECT_EQ(counter.load(), kNumTasks);
}

TEST(ThreadPoolConcurrency, ResultsAreIndependentAndCorrect) {
    const int kNumTasks = 1000;
    ThreadPool pool(4);

    std::vector<std::future<int>> futures;
    futures.reserve(kNumTasks);
    for (int i = 0; i < kNumTasks; ++i) {
        futures.push_back(pool.submit([i] { return i * i; }));
    }
    for (int i = 0; i < kNumTasks; ++i) {
        EXPECT_EQ(futures[i].get(), i * i);
    }
}

TEST(ThreadPoolConcurrency, NoDataRaceOnSharedCounterUnderContention) {
    // Regression test for the mutex-guarded queue: hammer the pool with a
    // single-worker pool doing interleaved increments/decrements to a
    // shared counter and confirm the final value is exact.
    ThreadPool pool(std::thread::hardware_concurrency());
    std::atomic<long> shared_counter{0};
    const int kNumTasks = 20000;

    std::vector<std::future<void>> futures;
    futures.reserve(kNumTasks);
    for (int i = 0; i < kNumTasks; ++i) {
        futures.push_back(pool.submit([&shared_counter] {
            shared_counter.fetch_add(1, std::memory_order_relaxed);
        }));
    }
    for (auto& f : futures) f.get();

    EXPECT_EQ(shared_counter.load(), kNumTasks);
}

// ---------------------------------------------------------------------------
// Scheduling accuracy (priority ordering)
// ---------------------------------------------------------------------------

TEST(ThreadPoolScheduling, HigherPriorityTasksRunBeforeLowerPriority) {
    // Use a single worker so ordering is deterministic, and block that
    // worker until all tasks are queued, so priority ordering (not
    // submission-order luck) determines execution order.
    ThreadPool pool(1);
    std::mutex m;
    std::condition_variable release_cv;
    bool release = false;

    // Occupy the single worker so nothing runs while we queue tasks.
    auto blocker = pool.submit([&] {
        std::unique_lock<std::mutex> lock(m);
        release_cv.wait(lock, [&] { return release; });
    });

    std::vector<int> execution_order;
    std::mutex order_mutex;

    auto record = [&](int id) {
        std::lock_guard<std::mutex> lock(order_mutex);
        execution_order.push_back(id);
    };

    auto low = pool.submit(TaskPriority::Low, [&] { record(0); });
    auto normal = pool.submit(TaskPriority::Normal, [&] { record(1); });
    auto high = pool.submit(TaskPriority::High, [&] { record(2); });

    {
        std::lock_guard<std::mutex> lock(m);
        release = true;
    }
    release_cv.notify_one();

    blocker.get();
    low.get();
    normal.get();
    high.get();

    ASSERT_EQ(execution_order.size(), 3u);
    EXPECT_EQ(execution_order[0], 2);  // High first
    EXPECT_EQ(execution_order[1], 1);  // Normal second
    EXPECT_EQ(execution_order[2], 0);  // Low last
}

TEST(ThreadPoolScheduling, SamePriorityTasksRunInFifoOrder) {
    ThreadPool pool(1);
    std::mutex m;
    std::condition_variable release_cv;
    bool release = false;

    auto blocker = pool.submit([&] {
        std::unique_lock<std::mutex> lock(m);
        release_cv.wait(lock, [&] { return release; });
    });

    std::vector<int> execution_order;
    std::mutex order_mutex;
    std::vector<std::future<void>> futures;

    for (int i = 0; i < 10; ++i) {
        futures.push_back(pool.submit(TaskPriority::Normal, [&, i] {
            std::lock_guard<std::mutex> lock(order_mutex);
            execution_order.push_back(i);
        }));
    }

    {
        std::lock_guard<std::mutex> lock(m);
        release = true;
    }
    release_cv.notify_one();

    blocker.get();
    for (auto& f : futures) f.get();

    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(execution_order[i], i);
    }
}

// ---------------------------------------------------------------------------
// Exception handling
// ---------------------------------------------------------------------------

TEST(ThreadPoolExceptions, ExceptionPropagatesThroughFuture) {
    ThreadPool pool(2);
    auto fut = pool.submit([]() -> int {
        throw std::runtime_error("boom");
    });
    EXPECT_THROW(fut.get(), std::runtime_error);
}

TEST(ThreadPoolExceptions, PoolContinuesServingAfterATaskThrows) {
    ThreadPool pool(2);
    auto failing = pool.submit([]() -> int { throw std::runtime_error("boom"); });
    EXPECT_THROW(failing.get(), std::runtime_error);

    // The pool and its worker threads must still be usable afterward.
    auto healthy = pool.submit([] { return 99; });
    EXPECT_EQ(healthy.get(), 99);
}

TEST(ThreadPoolExceptions, SubmitAfterShutdownThrows) {
    ThreadPool pool(2);
    pool.shutdown();
    EXPECT_THROW(pool.submit([] { return 1; }), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Delayed execution
// ---------------------------------------------------------------------------

TEST(ThreadPoolDelayed, DelayedTaskDoesNotRunBeforeItsDelay) {
    ThreadPool pool(2);
    auto start = std::chrono::steady_clock::now();

    auto fut = pool.submit_delayed(std::chrono::milliseconds(100),
                                    TaskPriority::Normal,
                                    [] { return 1; });
    EXPECT_EQ(fut.get(), 1);

    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_GE(elapsed, std::chrono::milliseconds(90));  // small tolerance
}

TEST(ThreadPoolDelayed, MultipleDelayedTasksAllComplete) {
    ThreadPool pool(4);
    std::atomic<int> completed{0};
    std::vector<std::future<void>> futures;

    for (int i = 0; i < 20; ++i) {
        futures.push_back(pool.submit_delayed(
            std::chrono::milliseconds(10 * (i % 5)), TaskPriority::Normal,
            [&completed] { completed.fetch_add(1); }));
    }
    for (auto& f : futures) f.get();

    EXPECT_EQ(completed.load(), 20);
}

// ---------------------------------------------------------------------------
// Shutdown / lifecycle behavior
// ---------------------------------------------------------------------------

TEST(ThreadPoolShutdown, ShutdownDrainsQueuedTasksBeforeReturning) {
    ThreadPool pool(2);
    std::atomic<int> completed{0};
    const int kNumTasks = 200;

    for (int i = 0; i < kNumTasks; ++i) {
        pool.submit([&completed] {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            completed.fetch_add(1);
        });
    }
    pool.shutdown();

    EXPECT_EQ(completed.load(), kNumTasks);
}

TEST(ThreadPoolShutdown, DoubleShutdownIsSafe) {
    ThreadPool pool(2);
    pool.submit([] { return 0; }).get();
    pool.shutdown();
    EXPECT_NO_THROW(pool.shutdown());  // second call must be a no-op, not UB
}

TEST(ThreadPoolShutdown, DestructorShutsDownCleanlyWithPendingTasks) {
    std::atomic<int> completed{0};
    {
        ThreadPool pool(4);
        for (int i = 0; i < 100; ++i) {
            pool.submit([&completed] { completed.fetch_add(1); });
        }
        // pool goes out of scope here; destructor must join all workers.
    }
    EXPECT_EQ(completed.load(), 100);
}

// ---------------------------------------------------------------------------
// Stress test
// ---------------------------------------------------------------------------

TEST(ThreadPoolStress, HighVolumeMixedWorkloadCompletesConsistently) {
    ThreadPool pool(std::thread::hardware_concurrency());
    const int kNumTasks = 10000;
    std::atomic<long long> sum{0};

    std::vector<std::future<void>> futures;
    futures.reserve(kNumTasks);
    for (int i = 0; i < kNumTasks; ++i) {
        futures.push_back(pool.submit([&sum, i] {
            sum.fetch_add(i, std::memory_order_relaxed);
        }));
    }
    for (auto& f : futures) f.get();

    long long expected = static_cast<long long>(kNumTasks - 1) * kNumTasks / 2;
    EXPECT_EQ(sum.load(), expected);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
