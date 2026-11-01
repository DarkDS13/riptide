#pragma once

// Timeline tracing: records what every thread does and when.
//
// While a trace::Session is active, Scope objects record an interval on the calling
// thread's row and instant() records a point event (a work steal, say). The session
// writes the Chrome trace-event format, which Perfetto (ui.perfetto.dev) and
// chrome://tracing show as one row per thread; bench/plot_trace.py draws the same
// file as an image.
//
// Recording is off by default. A Scope then costs one relaxed atomic load, so the
// instrumentation stays in the pipeline permanently. While recording, every thread
// appends to its own event list, so recording takes no lock either; a lock is taken
// only the first time a thread records in a session.
//
// A session must be started and stopped while no traced work is running: start it
// before a decompression and stop it after decompress() has returned.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>

namespace riptide::trace {

/// A named integer attached to an event, shown when the event is selected.
struct Arg {
    const char* name = nullptr;
    std::int64_t value = 0;
};

/// Nanoseconds on the clock all events use (std::chrono::steady_clock).
[[nodiscard]] std::uint64_t now_ns() noexcept;

/// True while a session is recording.
[[nodiscard]] bool active() noexcept;

/// Sets the row label of the calling thread for all later sessions, e.g. "worker 3".
/// Threads that never call it are labelled "main".
void name_thread(std::string name);

/// Records events from construction until stop() or destruction. At most one
/// session can be active at a time.
class Session {
public:
    /// Starts recording. Throws std::logic_error if another session is active.
    Session();
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    /// Stops recording; later events are not kept. Idempotent.
    void stop() noexcept;

    [[nodiscard]] std::size_t event_count() const;

    /// Writes all events as a Chrome trace-event JSON object. Times are relative to
    /// the start of the session.
    void write_json(std::ostream& out) const;

    struct State;

private:
    std::unique_ptr<State> state_;
};

/// Records the lifetime of the object as one interval on the calling thread.
class Scope {
public:
    Scope(const char* name, const char* category, Arg first = {}, Arg second = {}) noexcept;
    ~Scope();

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

    /// Replaces the second argument, for values known only at the end (output size).
    void set(Arg second) noexcept { second_ = second; }

private:
    Session::State* session_;
    const char* name_;
    const char* category_;
    Arg first_;
    Arg second_;
    std::uint64_t begin_ = 0;
};

/// Records a point event on the calling thread.
void instant(const char* name, const char* category, Arg first = {}, Arg second = {}) noexcept;

/// Records an interval measured elsewhere, on its own row `track` (for example the
/// GPU, whose command-buffer timestamps are converted to now_ns() by the caller).
void interval(const char* track, const char* name, const char* category, std::uint64_t begin_ns,
              std::uint64_t end_ns, Arg first = {}, Arg second = {}) noexcept;

}  // namespace riptide::trace
