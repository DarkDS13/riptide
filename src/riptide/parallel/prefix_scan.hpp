#pragma once

// Parallel inclusive prefix scans for an associative, not necessarily commutative,
// operation `combine(earlier, later)`.
//
// After a scan, values[k] == values[0] (+) values[1] (+) ... (+) values[k], where
// (+) is `combine` applied left to right. Because the operation need not be
// commutative, every algorithm here keeps operands in their original order; this
// is what allows it to compose window maps, where order matters.
//
// The algorithms trade total work against the length of the critical path:
//
//   algorithm        work          rounds (span)
//   sequential       n - 1         n - 1
//   Hillis-Steele    O(n log n)    ceil(log2 n)
//   Blelloch         O(n)          2 log2 n + 1
//
// The blocked scheme (about 2n work, 2n / p + p rounds for p blocks) is implemented
// for windows directly in window/window_resolver.cpp, where it needs no prefix maps.

#include <algorithm>
#include <bit>
#include <cstddef>
#include <vector>

#include <riptide/parallel/task_group.hpp>
#include <riptide/parallel/thread_pool.hpp>

namespace riptide {

/// Work and depth actually spent by a scan, reported in the statistics.
struct ScanCost {
    std::size_t rounds = 0;        ///< Dependent steps on the critical path.
    std::size_t combinations = 0;  ///< Calls to the combine operation.
};

template <typename T, typename Combine>
ScanCost sequential_inclusive_scan(std::vector<T>& values, Combine&& combine) {
    for (std::size_t k = 1; k < values.size(); ++k) {
        values[k] = combine(values[k - 1], values[k]);
    }
    const std::size_t steps = values.empty() ? 0 : values.size() - 1;
    return {steps, steps};
}

/// Hillis-Steele scan: in round d every element combines with the element 2^d
/// positions earlier, all elements of a round in parallel.
template <typename T, typename Combine>
ScanCost hillis_steele_inclusive_scan(ThreadPool& pool, std::vector<T>& values, Combine&& combine) {
    ScanCost cost;
    const std::size_t n = values.size();
    std::vector<T> next(values);
    for (std::size_t distance = 1; distance < n; distance *= 2) {
        parallel_for(pool, distance, n, 1, [&](std::size_t lo, std::size_t hi) {
            for (std::size_t k = lo; k < hi; ++k) {
                next[k] = combine(values[k - distance], values[k]);
            }
        });
        std::copy(next.begin() + static_cast<std::ptrdiff_t>(distance), next.end(),
                  values.begin() + static_cast<std::ptrdiff_t>(distance));
        cost.combinations += n - distance;
        ++cost.rounds;
    }
    return cost;
}

/// Blelloch's work-efficient scan: an up-sweep builds subtree totals in a balanced
/// binary tree, a down-sweep turns them into exclusive prefixes, and a final pass
/// makes them inclusive. `identity` must satisfy combine(identity, x) == x.
template <typename T, typename Combine>
ScanCost blelloch_inclusive_scan(ThreadPool& pool, std::vector<T>& values, Combine&& combine,
                                 const T& identity) {
    ScanCost cost;
    const std::size_t n = values.size();
    if (n <= 1) {
        return cost;
    }
    const std::size_t size = std::bit_ceil(n);
    std::vector<T> tree(values);
    tree.resize(size, identity);

    // Up-sweep: node at (k + 2d - 1) becomes the total of its two children.
    for (std::size_t half = 1; half < size; half *= 2) {
        const std::size_t span = half * 2;
        parallel_for(pool, 0, size / span, 1, [&](std::size_t lo, std::size_t hi) {
            for (std::size_t node = lo; node < hi; ++node) {
                const std::size_t right = node * span + span - 1;
                tree[right] = combine(tree[right - half], tree[right]);
            }
        });
        cost.combinations += size / span;
        ++cost.rounds;
    }

    // Down-sweep: a left child receives its parent's prefix; a right child receives
    // the parent's prefix followed by the left subtree's total.
    tree[size - 1] = identity;
    for (std::size_t half = size / 2; half >= 1; half /= 2) {
        const std::size_t span = half * 2;
        parallel_for(pool, 0, size / span, 1, [&](std::size_t lo, std::size_t hi) {
            for (std::size_t node = lo; node < hi; ++node) {
                const std::size_t right = node * span + span - 1;
                const std::size_t left = right - half;
                T left_total = tree[left];
                tree[left] = tree[right];
                tree[right] = combine(tree[right], left_total);
            }
        });
        cost.combinations += size / span;
        ++cost.rounds;
    }

    // Exclusive -> inclusive.
    parallel_for(pool, 0, n, 1, [&](std::size_t lo, std::size_t hi) {
        for (std::size_t k = lo; k < hi; ++k) {
            values[k] = combine(tree[k], values[k]);
        }
    });
    cost.combinations += n;
    ++cost.rounds;
    return cost;
}

}  // namespace riptide
