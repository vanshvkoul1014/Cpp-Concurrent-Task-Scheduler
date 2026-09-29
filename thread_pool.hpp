#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Concurrent Task Scheduler / Thread Pool
//
// Features:
//  - Configurable worker thread pool
//  - Thread-safe priority task queue (condition-variable dispatch)
//  - Generic async submission via templates + std::future
//  - Exception propagation through futures
//  - Delayed (scheduled) execution
//  - Graceful shutdown / worker lifecycle management
// ---------------------------------------------------------------------------

enum class TaskPriority : int { Low = 0, Normal = 1, High = 2 };

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads)
        : stop_(false), active_tasks_(0) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~ThreadPool() {
        shutdown();
    }

    // Submit a task for immediate (priority-ordered) execution.
    template <typename F, typename... Args>
    auto submit(TaskPriority priority, F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto task_ptr = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<ReturnType> result = task_ptr->get_future();

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) {
                throw std::runtime_error("submit() called on stopped ThreadPool");
            }
            tasks_.push(Entry{priority, seq_++, [task_ptr] { (*task_ptr)(); }});
        }
        cv_.notify_one();
        return result;
    }

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) {
        return submit(TaskPriority::Normal, std::forward<F>(f), std::forward<Args>(args)...);
    }

    // Submit a task to run after `delay` has elapsed.
    template <typename F, typename... Args>
    auto submit_delayed(std::chrono::milliseconds delay, TaskPriority priority,
                         F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto task_ptr = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<ReturnType> result = task_ptr->get_future();

        std::thread([this, delay, priority, task_ptr] {
            std::this_thread::sleep_for(delay);
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                if (stop_) return;
                tasks_.push(Entry{priority, seq_++, [task_ptr] { (*task_ptr)(); }});
            }
            cv_.notify_one();
        }).detach();

        return result;
    }

    size_t active_tasks() const { return active_tasks_.load(); }

    // Finish queued work, then join all workers.
    void shutdown() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) return;
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
    }

private:
    struct Entry {
        TaskPriority priority;
        uint64_t seq;  // tie-breaker for FIFO within same priority
        std::function<void()> fn;

        bool operator<(const Entry& other) const {
            if (priority != other.priority)
                return priority < other.priority;      // max-heap on priority
            return seq > other.seq;                     // min-heap on seq (FIFO)
        }
    };

    void worker_loop() {
        while (true) {
            Entry entry;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) return;
                entry = std::move(const_cast<Entry&>(tasks_.top()));
                tasks_.pop();
            }
            ++active_tasks_;
            try {
                entry.fn();
            } catch (...) {
                // Exception is captured inside the packaged_task's future.
            }
            --active_tasks_;
        }
    }

    std::vector<std::thread> workers_;
    std::priority_queue<Entry> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_;
    std::atomic<size_t> active_tasks_;
    uint64_t seq_ = 0;
};
