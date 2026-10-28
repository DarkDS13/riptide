#pragma once

// Parsing helpers shared by the riptide command-line tool and the benchmark driver.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <riptide/pipeline/options.hpp>
#include <riptide/window/window_resolver.hpp>

namespace riptide::cli {

/// Parses a size such as "4096", "512K", "4M" or "1G" (binary units; plain numbers
/// are bytes). Throws std::invalid_argument.
[[nodiscard]] std::size_t parse_size(std::string_view text);

[[nodiscard]] std::size_t parse_count(std::string_view text);
[[nodiscard]] double parse_number(std::string_view text);

[[nodiscard]] WindowStrategy parse_window_strategy(std::string_view text);
[[nodiscard]] ChunkingPolicy parse_chunking(std::string_view text);
[[nodiscard]] OverflowPolicy parse_overflow(std::string_view text);

/// Splits "a,b,c" into its elements.
[[nodiscard]] std::vector<std::string> split_list(std::string_view text);

/// Formats a byte count with a binary unit, e.g. "4 MiB".
[[nodiscard]] std::string format_size(std::size_t bytes);

}  // namespace riptide::cli
