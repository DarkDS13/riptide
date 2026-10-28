#pragma once

#include <chrono>

namespace riptide {

/// Wall-clock stopwatch used for per-stage timing.
class Stopwatch {
public:
    Stopwatch() noexcept : start_(Clock::now()) {}

    void restart() noexcept { start_ = Clock::now(); }

    /// Seconds elapsed since construction or the last restart().
    [[nodiscard]] double seconds() const noexcept {
        return std::chrono::duration<double>(Clock::now() - start_).count();
    }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point start_;
};

}  // namespace riptide
