// Tests of the parallel runtime and of window resolution: thread pool, task groups,
// parallel_for, the prefix scans, the window-map algebra behind M3, and tracing.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

#include <riptide/common/trace.hpp>
#include <riptide/gpu/gpu_window_resolver.hpp>
#include <riptide/parallel/prefix_scan.hpp>
#include <riptide/parallel/task_group.hpp>
#include <riptide/parallel/thread_pool.hpp>
#include <riptide/window/window.hpp>
#include <riptide/window/window_resolver.hpp>

#include "test_framework.hpp"

using namespace riptide;

namespace {

/// A random window map: each entry a literal or a marker, about `marker_share` markers.
SymbolWindowPtr random_map(std::mt19937& engine, double marker_share) {
    auto map = std::make_shared<SymbolWindow>();
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    bool any_marker = false;
    for (auto& symbol : map->symbols) {
        if (uniform(engine) < marker_share) {
            symbol = static_cast<std::uint16_t>(kMarkerBase + engine() % kWindowSize);
            any_marker = true;
        } else {
            symbol = static_cast<std::uint16_t>(engine() % 256);
        }
    }
    map->concrete = !any_marker;
    return map;
}

ByteWindow random_window(std::mt19937& engine) {
    ByteWindow window{};
    for (auto& byte : window) {
        byte = static_cast<std::uint8_t>(engine());
    }
    return window;
}

ByteWindow evaluate(const SymbolWindowPtr& map, const ByteWindow& input) {
    ByteWindow output{};
    apply(*map, input, output);
    return output;
}

}  // namespace

RIPTIDE_TEST(thread_pool, runs_every_task) {
    ThreadPool pool(4);
    std::atomic<int> sum{0};
    {
        TaskGroup group(pool);
        for (int i = 1; i <= 1000; ++i) {
            group.run([&sum, i] { sum += i; });
        }
        group.wait();
    }
    CHECK_EQ(sum.load(), 500500);
    CHECK(pool.statistics().tasks_executed >= 1000);
}

RIPTIDE_TEST(thread_pool, tasks_can_spawn_tasks_into_their_group) {
    ThreadPool pool(4);
    std::atomic<int> leaves{0};
    TaskGroup group(pool);
    std::function<void(int)> spawn = [&](int depth) {
        if (depth == 0) {
            ++leaves;
            return;
        }
        group.run([&spawn, depth] { spawn(depth - 1); });
        group.run([&spawn, depth] { spawn(depth - 1); });
    };
    group.run([&] { spawn(10); });
    group.wait();
    CHECK_EQ(leaves.load(), 1024);
}

RIPTIDE_TEST(thread_pool, propagates_the_first_exception) {
    ThreadPool pool(3);
    TaskGroup group(pool);
    for (int i = 0; i < 20; ++i) {
        group.run([i] {
            if (i == 7) {
                throw std::runtime_error("task failed");
            }
        });
    }
    CHECK_THROWS(group.wait(), std::runtime_error);
}

RIPTIDE_TEST(thread_pool, parallel_for_covers_the_range_exactly_once) {
    ThreadPool pool(6);
    for (const std::size_t n : {0U, 1U, 5U, 1000U, 12345U}) {
        std::vector<std::atomic<int>> hits(n);
        parallel_for(pool, 0, n, 7, [&](std::size_t lo, std::size_t hi) {
            for (std::size_t i = lo; i < hi; ++i) {
                ++hits[i];
            }
        });
        bool exactly_once = true;
        for (const auto& hit : hits) {
            exactly_once = exactly_once && hit.load() == 1;
        }
        CHECK(exactly_once);
    }
}

RIPTIDE_TEST(prefix_scan, all_algorithms_agree_on_a_non_commutative_operation) {
    // String concatenation is associative but not commutative, so any reordering
    // of operands by a scan shows up as a wrong result.
    auto concat = [](const std::string& earlier, const std::string& later) { return earlier + later; };
    ThreadPool pool(5);
    for (const std::size_t n : {1U, 2U, 3U, 7U, 8U, 9U, 33U, 100U}) {
        std::vector<std::string> input(n);
        for (std::size_t k = 0; k < n; ++k) {
            input[k] = std::string(1, static_cast<char>('a' + k % 26)) + std::to_string(k) + ";";
        }
        auto expected = input;
        sequential_inclusive_scan(expected, concat);

        auto hillis = input;
        hillis_steele_inclusive_scan(pool, hillis, concat);
        auto blelloch = input;
        blelloch_inclusive_scan(pool, blelloch, concat, std::string());
        auto blocked = input;
        blocked_inclusive_scan(pool, blocked, concat, 4);

        CHECK(hillis == expected);
        CHECK(blelloch == expected);
        CHECK(blocked == expected);
    }
}

RIPTIDE_TEST(prefix_scan, reports_logarithmic_depth) {
    ThreadPool pool(4);
    std::vector<int> values(1024, 1);
    auto add = [](int a, int b) { return a + b; };
    const ScanCost hillis = hillis_steele_inclusive_scan(pool, values, add);
    CHECK_EQ(hillis.rounds, 10U);
    CHECK_EQ(values.back(), 1024);

    std::vector<int> again(1024, 1);
    const ScanCost blelloch = blelloch_inclusive_scan(pool, again, add, 0);
    CHECK_EQ(blelloch.rounds, 21U);
    CHECK(blelloch.combinations < 3 * 1024);
    CHECK_EQ(again.back(), 1024);
}

RIPTIDE_TEST(window, composition_is_associative) {
    std::mt19937 engine(2024);
    for (int trial = 0; trial < 8; ++trial) {
        const auto a = random_map(engine, 0.3);
        const auto b = random_map(engine, 0.5);
        const auto c = random_map(engine, 0.2);
        const auto left = compose(compose(a, b), c);
        const auto right = compose(a, compose(b, c));
        CHECK(left->symbols == right->symbols);
    }
}

RIPTIDE_TEST(window, composition_matches_sequential_evaluation) {
    std::mt19937 engine(7);
    const auto a = random_map(engine, 0.4);
    const auto b = random_map(engine, 0.6);
    const ByteWindow w0 = random_window(engine);
    CHECK(evaluate(compose(a, b), w0) == evaluate(b, evaluate(a, w0)));
}

RIPTIDE_TEST(window, identity_is_neutral) {
    std::mt19937 engine(3);
    const auto a = random_map(engine, 0.5);
    CHECK(compose(identity_window(), a)->symbols == a->symbols);
    CHECK(compose(a, identity_window())->symbols == a->symbols);
    const ByteWindow w = random_window(engine);
    CHECK(evaluate(identity_window(), w) == w);
}

RIPTIDE_TEST(window_resolver, every_strategy_resolves_identical_windows) {
    std::mt19937 engine(11);
    for (const std::size_t chunks : {0U, 1U, 2U, 5U, 17U, 64U, 131U}) {
        std::vector<SymbolWindowPtr> maps;
        for (std::size_t k = 0; k < chunks; ++k) {
            // Mostly symbolic maps, some concrete ones, as in real data.
            maps.push_back(random_map(engine, k % 5 == 4 ? 0.0 : 0.3));
        }
        const ByteWindow first = random_window(engine);
        ThreadPool pool(4);
        const auto serial = resolve_windows(pool, first, maps, WindowStrategy::kSerial);
        for (const auto strategy : {WindowStrategy::kHillisSteele, WindowStrategy::kBlelloch,
                                    WindowStrategy::kBlocked}) {
            const auto other = resolve_windows(pool, first, maps, strategy);
            if (!std::ranges::equal(other.windows, serial.windows)) {
                test::report_failure(__FILE__, __LINE__,
                                     std::string(to_string(strategy)) + " differs for " +
                                         std::to_string(chunks) + " chunks");
            }
        }
    }
}

RIPTIDE_TEST(window_resolver, gpu_strategies_match_the_serial_chain) {
    if (!GpuWindowResolver::available()) {
        return;  // no GPU on this machine or in this build
    }
    const GpuWindowResolver gpu;
    std::mt19937 engine(13);
    ThreadPool pool(2);
    for (const std::size_t chunks : {0U, 1U, 2U, 3U, 17U, 64U, 131U, 500U}) {
        std::vector<SymbolWindowPtr> maps;
        for (std::size_t k = 0; k < chunks; ++k) {
            maps.push_back(random_map(engine, k % 5 == 4 ? 0.0 : 0.3));
        }
        const ByteWindow first = random_window(engine);
        const auto serial = resolve_windows(pool, first, maps, WindowStrategy::kSerial);
        for (const auto strategy : {WindowStrategy::kGpuSerial, WindowStrategy::kGpuBlocked}) {
            const GpuWindowResolver::Maps gpu_maps = gpu.allocate_maps(maps.size());
            for (std::size_t k = 0; k < maps.size(); ++k) {
                std::copy(maps[k]->symbols.begin(), maps[k]->symbols.end(), gpu_maps.map(k));
            }
            const auto other = gpu.resolve(first, gpu_maps, strategy);
            if (!std::ranges::equal(other.windows, serial.windows)) {
                test::report_failure(__FILE__, __LINE__,
                                     std::string(to_string(strategy)) + " differs for " +
                                         std::to_string(chunks) + " chunks");
            }
            if (strategy == WindowStrategy::kGpuSerial) {
                CHECK_EQ(other.cost.rounds, chunks);
            }
        }
    }
}

RIPTIDE_TEST(window_resolver, gpu_block_count_minimises_dependent_steps) {
    CHECK_EQ(gpu_block_count(WindowStrategy::kGpuSerial, 5000), 1U);
    CHECK_EQ(gpu_block_count(WindowStrategy::kGpuBlocked, 0), 1U);
    CHECK_EQ(gpu_block_count(WindowStrategy::kGpuBlocked, 1), 1U);
    CHECK_EQ(gpu_block_count(WindowStrategy::kGpuBlocked, 5000), 100U);  // sqrt(2 * 5000)
    ThreadPool pool(1);
    CHECK_THROWS(static_cast<void>(resolve_windows(pool, ByteWindow{}, {}, WindowStrategy::kGpuBlocked)),
                 std::invalid_argument);
}

RIPTIDE_TEST(trace, records_scopes_from_every_thread) {
    ThreadPool pool(3);
    std::string json;
    std::size_t events = 0;
    {
        trace::Session session;
        CHECK(trace::active());
        CHECK_THROWS(trace::Session{}, std::logic_error);
        parallel_for(pool, 0, 64, 1, [](std::size_t lo, std::size_t hi) {
            const trace::Scope scope("unit of work", "test", {"items", static_cast<std::int64_t>(hi - lo)});
        });
        trace::instant("marker", "test");
        trace::interval("GPU", "kernel", "gpu", trace::now_ns(), trace::now_ns() + 1000);
        session.stop();
        CHECK(!trace::active());
        // Events after stop() are not kept.
        { const trace::Scope ignored("ignored", "test"); }
        events = session.event_count();
        std::ostringstream out;
        session.write_json(out);
        json = out.str();
    }
    CHECK(events >= 2 + 2);  // parallel_for pieces, the instant and the GPU interval
    CHECK(json.find("\"unit of work\"") != std::string::npos);
    CHECK(json.find("\"items\":") != std::string::npos);
    CHECK(json.find("\"GPU\"") != std::string::npos);
    CHECK(json.find("\"ignored\"") == std::string::npos);
    CHECK(json.find("\"worker ") != std::string::npos);
}

RIPTIDE_TEST(trace, scopes_are_free_without_a_session) {
    CHECK(!trace::active());
    const trace::Scope scope("not recorded", "test");
    trace::instant("not recorded", "test");
    trace::Session session;
    CHECK_EQ(session.event_count(), 0U);
}
