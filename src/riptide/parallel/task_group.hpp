#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <utility>

#include <riptide/parallel/thread_pool.hpp>

namespace riptide {

/// A set of tasks that can be waited for together.
///
/// Tasks may add further tasks to the same group while it runs (the decompressor
/// does this when it splits an oversized chunk). wait() returns once every task,
/// including those added later, has finished, and rethrows the first exception any
/// of them raised. A thread blocked in wait() executes queued tasks itself.
class TaskGroup {
public:
    explicit TaskGroup(ThreadPool& pool) noexcept : pool_(pool) {}

    ~TaskGroup() {
        try {
            wait();
        } catch (...) {
            // Destructors must not throw; call wait() explicitly to observe errors.
        }
    }

    TaskGroup(const TaskGroup&) = delete;
    TaskGroup& operator=(const TaskGroup&) = delete;

    void run(std::function<void()> work) {
        pending_.fetch_add(1);
        pool_.submit([this, work = std::move(work)] {
            try {
                work();
            } catch (...) {
                const std::lock_guard lock(mutex_);
                if (!error_) {
                    error_ = std::current_exception();
                }
            }
            if (pending_.fetch_sub(1) == 1) {
                const std::lock_guard lock(mutex_);
                done_.notify_all();
            }
        });
    }

    void wait() {
        while (pending_.load() > 0) {
            if (pool_.run_pending_task()) {
                continue;
            }
            std::unique_lock lock(mutex_);
            done_.wait(lock, [this] { return pending_.load() == 0; });
        }
        const std::lock_guard lock(mutex_);
        if (error_) {
            std::rethrow_exception(std::exchange(error_, nullptr));
        }
    }

private:
    ThreadPool& pool_;
    std::atomic<std::size_t> pending_{0};
    std::mutex mutex_;
    std::condition_variable done_;
    std::exception_ptr error_;
};

/// Calls `body(lo, hi)` on disjoint sub-ranges covering [begin, end) in parallel.
///
/// The range is cut into pieces of at least `grain` elements, and into about four
/// pieces per worker so that uneven pieces still balance out. The calling thread
/// takes part in the work.
template <typename Body>
void parallel_for(ThreadPool& pool, std::size_t begin, std::size_t end, std::size_t grain,
                  Body&& body) {
    if (end <= begin) {
        return;
    }
    const std::size_t count = end - begin;
    grain = grain == 0 ? 1 : grain;
    const std::size_t max_pieces = pool.size() * 4;
    const std::size_t pieces = std::min((count + grain - 1) / grain, max_pieces);
    if (pieces <= 1) {
        body(begin, end);
        return;
    }

    TaskGroup group(pool);
    for (std::size_t piece = 0; piece < pieces; ++piece) {
        const std::size_t lo = begin + count * piece / pieces;
        const std::size_t hi = begin + count * (piece + 1) / pieces;
        group.run([&body, lo, hi] { body(lo, hi); });
    }
    group.wait();
}

}  // namespace riptide
