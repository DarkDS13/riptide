#include <riptide/window/window_resolver.hpp>

#include <algorithm>

#include <riptide/parallel/task_group.hpp>

namespace riptide {
namespace {

/// Base paper: W_k = f_k(W_{k-1}), strictly one after another.
WindowResolution resolve_serial(const ByteWindow& first, std::span<const SymbolWindowPtr> maps) {
    WindowResolution result;
    result.windows.resize(maps.size() + 1);
    result.windows[0] = first;
    for (std::size_t k = 1; k <= maps.size(); ++k) {
        apply(*maps[k - 1], result.windows[k - 1], result.windows[k]);
    }
    result.cost = {maps.size(), maps.size()};
    return result;
}

/// Scans the maps into prefix maps P_k = f_k o ... o f_1, then evaluates every
/// W_k = P_k(W_0) independently in parallel.
template <typename Scan>
WindowResolution resolve_by_scan(ThreadPool& pool, const ByteWindow& first,
                                 std::span<const SymbolWindowPtr> maps, Scan&& scan) {
    std::vector<SymbolWindowPtr> prefix(maps.begin(), maps.end());
    WindowResolution result;
    result.cost = scan(prefix);

    result.windows.resize(maps.size() + 1);
    result.windows[0] = first;
    parallel_for(pool, 0, prefix.size(), 1, [&](std::size_t lo, std::size_t hi) {
        for (std::size_t k = lo; k < hi; ++k) {
            apply(*prefix[k], first, result.windows[k + 1]);
        }
    });
    ++result.cost.rounds;
    return result;
}

/// Blocked resolution, specialised for windows so that no prefix maps are stored:
///   1. in parallel, each block composes its maps into one block map T_b;
///   2. serially, the window entering each block is S_b = T_{b-1}(S_{b-1});
///   3. in parallel, each block resolves its own windows starting from S_b.
WindowResolution resolve_blocked(ThreadPool& pool, const ByteWindow& first,
                                 std::span<const SymbolWindowPtr> maps) {
    const std::size_t n = maps.size();
    const std::size_t blocks = std::clamp<std::size_t>(pool.size(), 1, std::max<std::size_t>(n, 1));
    auto block_begin = [&](std::size_t b) { return n * b / blocks; };

    WindowResolution result;
    result.windows.resize(n + 1);
    result.windows[0] = first;
    if (n == 0) {
        return result;
    }

    std::vector<SymbolWindowPtr> block_maps(blocks);
    parallel_for(pool, 0, blocks, 1, [&](std::size_t lo, std::size_t hi) {
        for (std::size_t b = lo; b < hi; ++b) {
            SymbolWindowPtr total = identity_window();
            for (std::size_t k = block_begin(b); k < block_begin(b + 1); ++k) {
                total = compose(total, maps[k]);
            }
            block_maps[b] = std::move(total);
        }
    });

    // entering[b] is the window before the first chunk of block b.
    std::vector<ByteWindow> entering(blocks);
    entering[0] = first;
    for (std::size_t b = 1; b < blocks; ++b) {
        apply(*block_maps[b - 1], entering[b - 1], entering[b]);
    }

    parallel_for(pool, 0, blocks, 1, [&](std::size_t lo, std::size_t hi) {
        for (std::size_t b = lo; b < hi; ++b) {
            const ByteWindow* previous = &entering[b];
            for (std::size_t k = block_begin(b); k < block_begin(b + 1); ++k) {
                apply(*maps[k], *previous, result.windows[k + 1]);
                previous = &result.windows[k + 1];
            }
        }
    });

    const std::size_t longest_block = (n + blocks - 1) / blocks;
    result.cost.rounds = longest_block + (blocks - 1) + longest_block;
    result.cost.combinations = n + (blocks - 1) + n;
    return result;
}

}  // namespace

std::string_view to_string(WindowStrategy strategy) noexcept {
    switch (strategy) {
        case WindowStrategy::kSerial:
            return "serial";
        case WindowStrategy::kHillisSteele:
            return "hillis-steele";
        case WindowStrategy::kBlelloch:
            return "blelloch";
        case WindowStrategy::kBlocked:
            return "blocked";
    }
    return "unknown";
}

WindowResolution resolve_windows(ThreadPool& pool, const ByteWindow& first,
                                 std::span<const SymbolWindowPtr> maps, WindowStrategy strategy) {
    switch (strategy) {
        case WindowStrategy::kSerial:
            return resolve_serial(first, maps);
        case WindowStrategy::kHillisSteele:
            return resolve_by_scan(pool, first, maps, [&](std::vector<SymbolWindowPtr>& values) {
                return hillis_steele_inclusive_scan(pool, values, compose);
            });
        case WindowStrategy::kBlelloch:
            return resolve_by_scan(pool, first, maps, [&](std::vector<SymbolWindowPtr>& values) {
                return blelloch_inclusive_scan(pool, values, compose, identity_window());
            });
        case WindowStrategy::kBlocked:
            return resolve_blocked(pool, first, maps);
    }
    return resolve_serial(first, maps);
}

}  // namespace riptide
