#include <riptide/common/trace.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace riptide::trace {

struct Event {
    const char* name;
    const char* category;
    std::uint64_t begin;
    std::uint64_t end;
    Arg args[2];
    bool instant;
};

/// Events of one row: a thread, or a track such as the GPU.
struct Row {
    std::string label;
    std::vector<Event> events;
};

struct Session::State {
    std::uint64_t id = 0;
    std::uint64_t start = 0;
    std::mutex mutex;  // guards rows and tracks, not the events of a row
    std::vector<std::unique_ptr<Row>> rows;
    std::map<std::string, Row*, std::less<>> tracks;
    /// Rows of tracks share one lock, as several threads may add to a track.
    std::mutex track_mutex;
};

namespace {

std::atomic<Session::State*> active_session{nullptr};
std::atomic<std::uint64_t> next_session_id{1};

thread_local std::string thread_label;
thread_local std::uint64_t thread_session_id = 0;
thread_local Row* thread_row = nullptr;

/// The calling thread's row in `session`, created on its first event.
Row& row_of(Session::State& session) {
    if (thread_session_id != session.id) {
        const std::lock_guard lock(session.mutex);
        auto row = std::make_unique<Row>();
        row->label = thread_label.empty() ? "main" : thread_label;
        thread_row = session.rows.emplace_back(std::move(row)).get();
        thread_session_id = session.id;
    }
    return *thread_row;
}

/// Orders rows main, workers by number, then tracks.
int sort_index(const std::string& label) {
    if (label == "main") {
        return 0;
    }
    if (label.rfind("worker ", 0) == 0) {
        return 1 + std::atoi(label.c_str() + 7);
    }
    return 1000;
}

void write_string(std::ostream& out, const std::string& text) {
    out << '"';
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out << '\\';
        }
        out << c;
    }
    out << '"';
}

void write_microseconds(std::ostream& out, std::uint64_t nanoseconds) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.3f", static_cast<double>(nanoseconds) / 1000.0);
    out << text;
}

}  // namespace

std::uint64_t now_ns() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

bool active() noexcept { return active_session.load(std::memory_order_relaxed) != nullptr; }

void name_thread(std::string name) { thread_label = std::move(name); }

Session::Session() : state_(std::make_unique<State>()) {
    state_->id = next_session_id.fetch_add(1);
    state_->start = now_ns();
    State* expected = nullptr;
    if (!active_session.compare_exchange_strong(expected, state_.get())) {
        throw std::logic_error("a trace session is already active");
    }
}

Session::~Session() { stop(); }

void Session::stop() noexcept {
    State* expected = state_.get();
    active_session.compare_exchange_strong(expected, nullptr);
}

std::size_t Session::event_count() const {
    const std::lock_guard lock(state_->mutex);
    std::size_t count = 0;
    for (const auto& row : state_->rows) {
        count += row->events.size();
    }
    return count;
}

void Session::write_json(std::ostream& out) const {
    const std::lock_guard lock(state_->mutex);
    const std::uint64_t start = state_->start;
    out << "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[";
    bool first = true;
    auto separator = [&] {
        out << (first ? "\n" : ",\n");
        first = false;
    };

    // Rows with the same label (a worker of an earlier pool, say) are merged.
    std::map<std::string, int> tids;
    for (const auto& row : state_->rows) {
        if (tids.try_emplace(row->label, static_cast<int>(tids.size()) + 1).second) {
            const int tid = tids[row->label];
            separator();
            out << "{\"ph\":\"M\",\"pid\":1,\"tid\":" << tid << ",\"name\":\"thread_name\",\"args\":{\"name\":";
            write_string(out, row->label);
            out << "}}";
            separator();
            out << "{\"ph\":\"M\",\"pid\":1,\"tid\":" << tid
                << ",\"name\":\"thread_sort_index\",\"args\":{\"sort_index\":" << sort_index(row->label)
                << "}}";
        }
    }
    separator();
    out << "{\"ph\":\"M\",\"pid\":1,\"name\":\"process_name\",\"args\":{\"name\":\"riptide\"}}";

    for (const auto& row : state_->rows) {
        const int tid = tids[row->label];
        for (const Event& event : row->events) {
            separator();
            out << "{\"name\":";
            write_string(out, event.name);
            out << ",\"cat\":";
            write_string(out, event.category);
            out << ",\"ph\":\"" << (event.instant ? "i" : "X") << "\",\"pid\":1,\"tid\":" << tid
                << ",\"ts\":";
            write_microseconds(out, event.begin >= start ? event.begin - start : 0);
            if (event.instant) {
                out << ",\"s\":\"t\"";
            } else {
                out << ",\"dur\":";
                write_microseconds(out, event.end >= event.begin ? event.end - event.begin : 0);
            }
            if (event.args[0].name != nullptr || event.args[1].name != nullptr) {
                out << ",\"args\":{";
                bool first_arg = true;
                for (const Arg& arg : event.args) {
                    if (arg.name != nullptr) {
                        out << (first_arg ? "" : ",");
                        write_string(out, arg.name);
                        out << ':' << arg.value;
                        first_arg = false;
                    }
                }
                out << '}';
            }
            out << '}';
        }
    }
    out << "\n]}\n";
}

Scope::Scope(const char* name, const char* category, Arg first, Arg second) noexcept
    : session_(active_session.load(std::memory_order_relaxed)),
      name_(name),
      category_(category),
      first_(first),
      second_(second) {
    if (session_ != nullptr) {
        begin_ = now_ns();
    }
}

Scope::~Scope() {
    if (session_ == nullptr) {
        return;
    }
    const std::uint64_t end = now_ns();
    try {
        row_of(*session_).events.push_back({name_, category_, begin_, end, {first_, second_}, false});
    } catch (...) {
        // Out of memory while tracing: the event is dropped.
    }
}

void instant(const char* name, const char* category, Arg first, Arg second) noexcept {
    Session::State* session = active_session.load(std::memory_order_relaxed);
    if (session == nullptr) {
        return;
    }
    const std::uint64_t time = now_ns();
    try {
        row_of(*session).events.push_back({name, category, time, time, {first, second}, true});
    } catch (...) {
    }
}

void interval(const char* track, const char* name, const char* category, std::uint64_t begin_ns,
              std::uint64_t end_ns, Arg first, Arg second) noexcept {
    Session::State* session = active_session.load(std::memory_order_relaxed);
    if (session == nullptr) {
        return;
    }
    try {
        Row* row = nullptr;
        {
            const std::lock_guard lock(session->mutex);
            auto found = session->tracks.find(track);
            if (found == session->tracks.end()) {
                auto created = std::make_unique<Row>();
                created->label = track;
                found = session->tracks.emplace(track, created.get()).first;
                session->rows.push_back(std::move(created));
            }
            row = found->second;
        }
        const std::lock_guard lock(session->track_mutex);
        row->events.push_back({name, category, begin_ns, end_ns, {first, second}, false});
    } catch (...) {
    }
}

}  // namespace riptide::trace
