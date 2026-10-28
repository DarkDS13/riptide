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
//   blocked          about 2n      2n / p + p   (p = number of blocks)

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

/// Two-level scan: each of `blocks` contiguous blocks is scanned sequentially in
/// parallel, the block totals are scanned sequentially, and each block is then
/// offset by the total of all blocks before it, again in parallel.
template <typename T, typename Combine>
ScanCost blocked_inclusive_scan(ThreadPool& pool, std::vector<T>& values, Combine&& combine,
                                std::size_t blocks) {
    ScanCost cost;
    const std::size_t n = values.size();
    blocks = std::clamp<std::size_t>(blocks, 1, std::max<std::size_t>(n, 1));
    if (n <= 1) {
        return cost;
    }
    auto block_begin = [&](std::size_t b) { return n * b / blocks; };

    parallel_for(pool, 0, blocks, 1, [&](std::size_t lo, std::size_t hi) {
        for (std::size_t b = lo; b < hi; ++b) {
            for (std::size_t k = block_begin(b) + 1; k < block_begin(b + 1); ++k) {
                values[k] = combine(values[k - 1], values[k]);
            }
        }
    });

    // carry[b] = combination of all blocks before block b (block 0 has none).
    std::vector<T> carry(blocks);
    for (std::size_t b = 1; b < blocks; ++b) {
        const T& previous_total = values[block_begin(b) - 1];
        carry[b] = b == 1 ? previous_total : combine(carry[b - 1], previous_total);
    }

    parallel_for(pool, 1, blocks, 1, [&](std::size_t lo, std::size_t hi) {
        for (std::size_t b = lo; b < hi; ++b) {
            for (std::size_t k = block_begin(b); k < block_begin(b + 1); ++k) {
                values[k] = combine(carry[b], values[k]);
            }
        }
    });

    const std::size_t longest_block = (n + blocks - 1) / blocks;
    const std::size_t carry_steps = blocks >= 2 ? blocks - 2 : 0;
    cost.rounds = (longest_block - 1) + carry_steps + (blocks >= 2 ? longest_block : 0);
    cost.combinations = (n - blocks) + carry_steps + (n - block_begin(1));
    return cost;
}

}  // namespace riptide
