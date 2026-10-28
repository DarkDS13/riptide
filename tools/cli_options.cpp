#include "cli_options.hpp"

#include <cctype>
#include <charconv>
#include <stdexcept>

namespace riptide::cli {
namespace {

[[noreturn]] void fail(std::string_view what, std::string_view text) {
    throw std::invalid_argument("invalid " + std::string(what) + ": '" + std::string(text) + "'");
}

}  // namespace

std::size_t parse_count(std::string_view text) {
    std::size_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        fail("number", text);
    }
    return value;
}

double parse_number(std::string_view text) {
    try {
        std::size_t used = 0;
        const double value = std::stod(std::string(text), &used);
        if (used != text.size()) {
            fail("number", text);
        }
        return value;
    } catch (const std::logic_error&) {
        fail("number", text);
    }
}

std::size_t parse_size(std::string_view text) {
    if (text.empty()) {
        fail("size", text);
    }
    std::size_t multiplier = 1;
    switch (std::toupper(static_cast<unsigned char>(text.back()))) {
        case 'K':
            multiplier = std::size_t{1} << 10;
            break;
        case 'M':
            multiplier = std::size_t{1} << 20;
            break;
        case 'G':
            multiplier = std::size_t{1} << 30;
            break;
        default:
            break;
    }
    if (multiplier != 1) {
        text.remove_suffix(1);
    }
    return parse_count(text) * multiplier;
}

WindowStrategy parse_window_strategy(std::string_view text) {
    for (const auto strategy : {WindowStrategy::kSerial, WindowStrategy::kHillisSteele,
                                WindowStrategy::kBlelloch, WindowStrategy::kBlocked}) {
        if (text == to_string(strategy)) {
            return strategy;
        }
    }
    fail("window strategy (serial, hillis-steele, blelloch, blocked)", text);
}

ChunkingPolicy parse_chunking(std::string_view text) {
    for (const auto policy : {ChunkingPolicy::kFixed, ChunkingPolicy::kWorkBalanced}) {
        if (text == to_string(policy)) {
            return policy;
        }
    }
    fail("chunking policy (fixed, work-balanced)", text);
}

OverflowPolicy parse_overflow(std::string_view text) {
    for (const auto policy : {OverflowPolicy::kSerial, OverflowPolicy::kSplit}) {
        if (text == to_string(policy)) {
            return policy;
        }
    }
    fail("overflow policy (serial, split)", text);
}

std::vector<std::string> split_list(std::string_view text) {
    std::vector<std::string> items;
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        items.emplace_back(text.substr(0, comma));
        if (comma == std::string_view::npos) {
            break;
        }
        text.remove_prefix(comma + 1);
    }
    return items;
}

std::string format_size(std::size_t bytes) {
    if (bytes % (std::size_t{1} << 20) == 0 && bytes != 0) {
        return std::to_string(bytes >> 20) + " MiB";
    }
    if (bytes % (std::size_t{1} << 10) == 0 && bytes != 0) {
        return std::to_string(bytes >> 10) + " KiB";
    }
    return std::to_string(bytes) + " B";
}

}  // namespace riptide::cli
