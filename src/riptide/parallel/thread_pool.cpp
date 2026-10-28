#include <riptide/parallel/thread_pool.hpp>

#include <algorithm>
#include <chrono>

namespace riptide {
namespace {

/// Identifies the pool and queue index of the current thread, if it is a worker.
thread_local const ThreadPool* current_pool = nullptr;
thread_local std::size_t current_index = 0;

}  // namespace

ThreadPool::ThreadPool(std::size_t thread_count)
    : busy_nanoseconds_(std::max<std::size_t>(thread_count, 1)) {
    thread_count = std::max<std::size_t>(thread_count, 1);
    queues_.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i) {
        queues_.push_back(std::make_unique<WorkerQueue>());
    }
    workers_.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i) {
        workers_.emplace_back([this, i] { worker_loop(i); });
    }
}

ThreadPool::~ThreadPool() {
    {
        const std::lock_guard lock(sleep_mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    for (auto& worker : workers_) {
        worker.join();
    }
}

void ThreadPool::submit(Task task) {
    if (current_pool == this) {
        WorkerQueue& own = *queues_[current_index];
        const std::lock_guard lock(own.mutex);
        own.tasks.push_front(std::move(task));
    } else {
        WorkerQueue& target = *queues_[next_queue_.fetch_add(1) % queues_.size()];
        const std::lock_guard lock(target.mutex);
        target.tasks.push_back(std::move(task));
    }
    {
        // Incrementing under the sleep mutex means a worker that has just found
        // queued_ == 0 cannot miss this notification.
        const std::lock_guard lock(sleep_mutex_);
        queued_.fetch_add(1);
    }
    wake_.notify_one();
}

bool ThreadPool::run_pending_task() {
    Task task;
    const std::size_t index = current_pool == this ? current_index : 0;
    if ((current_pool == this && pop_own(index, task)) || steal(index, task)) {
        queued_.fetch_sub(1);
        executed_.fetch_add(1, std::memory_order_relaxed);
        task();
        return true;
    }
    return false;
}

ThreadPoolStatistics ThreadPool::statistics() const noexcept {
    return {executed_.load(std::memory_order_relaxed), stolen_.load(std::memory_order_relaxed)};
}

std::vector<double> ThreadPool::busy_seconds() const {
    std::vector<double> seconds;
    seconds.reserve(busy_nanoseconds_.size());
    for (const auto& nanoseconds : busy_nanoseconds_) {
        seconds.push_back(static_cast<double>(nanoseconds.load(std::memory_order_relaxed)) * 1e-9);
    }
    return seconds;
}

std::size_t ThreadPool::hardware_threads() noexcept {
    return std::max<std::size_t>(std::thread::hardware_concurrency(), 1);
}

void ThreadPool::worker_loop(std::size_t index) {
    current_pool = this;
    current_index = index;
    for (;;) {
        Task task;
        if (pop_own(index, task) || steal(index, task)) {
            queued_.fetch_sub(1);
            // Counted when started: a group's wait() may return before this worker
            // gets back here, and the statistics must already include the task then.
            executed_.fetch_add(1, std::memory_order_relaxed);
            const auto start = std::chrono::steady_clock::now();
            task();
            const auto elapsed = std::chrono::steady_clock::now() - start;
            busy_nanoseconds_[index].fetch_add(
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()),
                std::memory_order_relaxed);
            continue;
        }
        std::unique_lock lock(sleep_mutex_);
        wake_.wait(lock, [this] { return stopping_ || queued_.load() > 0; });
        if (stopping_ && queued_.load() == 0) {
            return;
        }
    }
}

bool ThreadPool::pop_own(std::size_t index, Task& task) {
    WorkerQueue& own = *queues_[index];
    const std::lock_guard lock(own.mutex);
    if (own.tasks.empty()) {
        return false;
    }
    task = std::move(own.tasks.front());
    own.tasks.pop_front();
    return true;
}

bool ThreadPool::steal(std::size_t thief, Task& task) {
    const std::size_t count = queues_.size();
    for (std::size_t offset = 1; offset <= count; ++offset) {
        const std::size_t victim = (thief + offset) % count;
        if (victim == thief && current_pool == this) {
            continue;  // a worker's own queue is handled by pop_own()
        }
        WorkerQueue& queue = *queues_[victim];
        const std::lock_guard lock(queue.mutex);
        if (!queue.tasks.empty()) {
            task = std::move(queue.tasks.back());
            queue.tasks.pop_back();
            stolen_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    }
    return false;
}

}  // namespace riptide
