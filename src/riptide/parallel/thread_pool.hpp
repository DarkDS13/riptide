#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace riptide {

/// Counters describing how work was distributed across the pool.
struct ThreadPoolStatistics {
    std::uint64_t tasks_executed = 0;  ///< Tasks started by any thread.
    std::uint64_t tasks_stolen = 0;  ///< Tasks run by a thread other than the one they were queued on.
};

/// Work-stealing thread pool.
///
/// Every worker owns a double-ended queue. A task submitted from a worker goes to
/// the front of that worker's own queue and is taken from the front again (LIFO),
/// which keeps recently produced data in that core's cache. Tasks submitted from
/// outside the pool are distributed round-robin. An idle worker steals from the
/// back (the oldest end) of other workers' queues, so large, early tasks migrate to
/// idle cores while each worker keeps working on its newest tasks.
///
/// Queues are protected by one mutex each; tasks in this project are coarse (a
/// chunk of megabytes, or a 32 KiB window composition), so a lock-free deque would
/// not pay off.
class ThreadPool {
public:
    using Task = std::function<void()>;

    /// Starts `thread_count` workers (at least one).
    explicit ThreadPool(std::size_t thread_count);

    /// Finishes all queued tasks, then joins the workers.
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// Queues a task. Safe to call from any thread, including from inside a task.
    void submit(Task task);

    /// Runs one queued task on the calling thread, if there is one. Threads that
    /// wait for a group of tasks call this so they help instead of blocking.
    bool run_pending_task();

    [[nodiscard]] std::size_t size() const noexcept { return workers_.size(); }

    [[nodiscard]] ThreadPoolStatistics statistics() const noexcept;

    /// Number of hardware threads, or 1 if unknown.
    [[nodiscard]] static std::size_t hardware_threads() noexcept;

private:
    struct WorkerQueue {
        std::mutex mutex;
        std::deque<Task> tasks;
    };

    void worker_loop(std::size_t index);
    bool pop_own(std::size_t index, Task& task);
    bool steal(std::size_t thief, Task& task);

    std::vector<std::unique_ptr<WorkerQueue>> queues_;
    std::vector<std::thread> workers_;

    std::mutex sleep_mutex_;
    std::condition_variable wake_;
    bool stopping_ = false;  // guarded by sleep_mutex_

    std::atomic<std::size_t> queued_{0};
    std::atomic<std::size_t> next_queue_{0};
    std::atomic<std::uint64_t> executed_{0};
    std::atomic<std::uint64_t> stolen_{0};
};

}  // namespace riptide
