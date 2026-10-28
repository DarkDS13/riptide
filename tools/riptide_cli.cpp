// riptide: decompress a gzip file in parallel.

#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <riptide/common/error.hpp>
#include <riptide/common/timer.hpp>
#include <riptide/io/mapped_file.hpp>
#include <riptide/pipeline/parallel_decompressor.hpp>
#include <riptide/pipeline/sequential_decompressor.hpp>

#include "cli_options.hpp"

namespace {

constexpr std::string_view kUsage = R"(Usage: riptide [options] FILE.gz

Decompresses FILE.gz in parallel and writes the result to standard output.

Output:
  -o, --output FILE        write to FILE instead of standard output
  -t, --test               decompress and verify only; write nothing

Parallelism:
  -P, --threads N          worker threads (default: all hardware threads)
      --chunk-size SIZE    compressed bytes per chunk, e.g. 512K or 4M (default: 4M)
      --window STRATEGY    window resolution: serial, hillis-steele, blelloch,
                           blocked (default: blocked)
      --chunking POLICY    fixed or work-balanced (default: fixed)
      --overflow POLICY    serial or split (default: split)
      --max-expansion R    output budget per chunk, as a multiple of its
                           compressed size (default: 20)
      --sequential         use the single-threaded reference decoder

Verification and statistics:
      --no-verify          skip CRC-32 and size checks
  -v, --verbose            print per-stage timings to standard error
      --json               print statistics as JSON to standard error
  -h, --help               show this help
)";

struct Arguments {
    std::string input;
    std::string output;
    bool test_only = false;
    bool sequential = false;
    bool verbose = false;
    bool json = false;
    riptide::DecompressorOptions options;
};

Arguments parse_arguments(int argc, char** argv) {
    namespace cli = riptide::cli;
    Arguments args;
    const std::vector<std::string_view> tokens(argv + 1, argv + argc);
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string_view token = tokens[i];
        auto value = [&]() -> std::string_view {
            if (i + 1 >= tokens.size()) {
                throw std::invalid_argument("missing value for " + std::string(token));
            }
            return tokens[++i];
        };

        if (token == "-h" || token == "--help") {
            std::cout << kUsage;
            std::exit(0);
        } else if (token == "-o" || token == "--output") {
            args.output = value();
        } else if (token == "-t" || token == "--test") {
            args.test_only = true;
        } else if (token == "-P" || token == "--threads") {
            args.options.threads = cli::parse_count(value());
        } else if (token == "--chunk-size") {
            args.options.chunk_size = cli::parse_size(value());
        } else if (token == "--window") {
            args.options.window_strategy = cli::parse_window_strategy(value());
        } else if (token == "--chunking") {
            args.options.chunking = cli::parse_chunking(value());
        } else if (token == "--overflow") {
            args.options.overflow = cli::parse_overflow(value());
        } else if (token == "--max-expansion") {
            args.options.max_expansion = cli::parse_number(value());
        } else if (token == "--sequential") {
            args.sequential = true;
        } else if (token == "--no-verify") {
            args.options.verify = false;
        } else if (token == "-v" || token == "--verbose") {
            args.verbose = true;
        } else if (token == "--json") {
            args.json = true;
        } else if (!token.empty() && token.front() == '-') {
            throw std::invalid_argument("unknown option " + std::string(token));
        } else if (args.input.empty()) {
            args.input = token;
        } else {
            throw std::invalid_argument("only one input file is supported");
        }
    }
    if (args.input.empty()) {
        throw std::invalid_argument("no input file given");
    }
    if (args.options.threads == 0 || args.options.chunk_size == 0) {
        throw std::invalid_argument("threads and chunk size must be positive");
    }
    return args;
}

void write_all(std::span<const std::uint8_t> data, const std::string& path) {
    std::FILE* file = path.empty() ? stdout : std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        throw std::runtime_error("cannot open '" + path + "' for writing: " + std::strerror(errno));
    }
    const std::size_t written = std::fwrite(data.data(), 1, data.size(), file);
    const bool failed = written != data.size() || std::fflush(file) != 0;
    if (file != stdout) {
        std::fclose(file);
    }
    if (failed) {
        throw std::runtime_error("write failed");
    }
}

void print_statistics(const riptide::DecompressionStatistics& s, const riptide::DecompressorOptions& o) {
    std::fprintf(stderr,
                 "riptide: %zu threads, %s chunks (%s), window %s, overflow %s\n"
                 "  %-22s %9.4f s\n  %-22s %9.4f s\n  %-22s %9.4f s\n  %-22s %9.4f s\n"
                 "  %-22s %9.4f s\n  %-22s %9.4f s\n  %-22s %9.4f s\n  %-22s %9.4f s\n"
                 "  %-22s %9.1f MB/s\n"
                 "  chunks: %zu planned, %zu in chain, %zu fallback, %zu overflow splits, "
                 "%zu unused, %zu rejected candidates\n"
                 "  window resolution: %zu rounds, %zu compositions; members: %zu\n"
                 "  first-stage load imbalance: %.2f\n"
                 "  thread pool: %llu tasks, %llu stolen\n",
                 o.threads, riptide::cli::format_size(o.chunk_size).c_str(),
                 std::string(riptide::to_string(o.chunking)).c_str(),
                 std::string(riptide::to_string(o.window_strategy)).c_str(),
                 std::string(riptide::to_string(o.overflow)).c_str(), "partition", s.partition_seconds,
                 "decode (first stage)", s.decode_seconds, "chain", s.chain_seconds,
                 "window maps", s.window_map_seconds, "window resolution", s.window_seconds,
                 "replace (second stage)", s.replace_seconds, "checksum", s.checksum_seconds, "total",
                 s.total_seconds, "throughput", s.throughput_mb_per_s(), s.planned_chunks,
                 s.chain_length, s.fallback_decodes, s.overflow_splits, s.unused_results,
                 s.rejected_candidates, s.window_rounds, s.window_combinations, s.members,
                 s.decode_imbalance,
                 static_cast<unsigned long long>(s.pool.tasks_executed),
                 static_cast<unsigned long long>(s.pool.tasks_stolen));
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments args = parse_arguments(argc, argv);
        const riptide::MappedFile input(args.input);

        if (args.sequential) {
            const riptide::Stopwatch timer;
            const auto output = riptide::decompress_sequential(input.bytes(), args.options.verify);
            if (args.verbose) {
                std::fprintf(stderr, "riptide: sequential, %.4f s, %.1f MB/s\n", timer.seconds(),
                             static_cast<double>(output.size()) / timer.seconds() / 1e6);
            }
            if (!args.test_only) {
                write_all(output, args.output);
            }
            return 0;
        }

        riptide::ParallelDecompressor decompressor(args.options);
        const riptide::DecompressionResult result = decompressor.decompress(input.bytes());
        if (args.verbose) {
            print_statistics(result.statistics, args.options);
        }
        if (args.json) {
            std::fprintf(stderr, "%s\n", result.statistics.to_json().c_str());
        }
        if (!args.test_only) {
            write_all(result.data(), args.output);
        }
        return 0;
    } catch (const riptide::DecodeError& error) {
        std::fprintf(stderr, "riptide: invalid input: %s\n", error.what());
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "riptide: %s\n", error.what());
        return 1;
    }
}
