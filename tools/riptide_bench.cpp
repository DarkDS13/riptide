// riptide_bench: repeatable in-process benchmarks of riptide against baselines.
//
// Every configuration in the cross product of the given lists is run `--warmup`
// times unmeasured and `--repetitions` times measured, with repetitions interleaved
// across configurations. Each measured run is checked byte for byte against zlib's
// output and written as one CSV row, from which bench/analyze.py computes medians,
// IQRs and significance tests.

#include <zlib.h>

#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <riptide/common/timer.hpp>
#include <riptide/io/mapped_file.hpp>
#include <riptide/pipeline/parallel_decompressor.hpp>
#include <riptide/pipeline/sequential_decompressor.hpp>

#include "cli_options.hpp"

namespace {

constexpr std::string_view kUsage = R"(Usage: riptide_bench [options] FILE.gz...

Lists are comma-separated; every combination is measured.
  --threads LIST           worker threads (default: 1,2,4,8,<hardware>)
  --chunk-sizes LIST       chunk sizes, e.g. 512K,4M (default: 4M)
  --windows LIST           serial,hillis-steele,blelloch,blocked (default: serial,blocked)
  --chunking LIST          fixed,work-balanced (default: fixed)
  --overflow LIST          serial,split (default: split)
  --max-expansion R        output budget multiple (default: 20)
  --repetitions N          measured runs per configuration (default: 10)
  --warmup N               unmeasured runs per configuration (default: 1)
  --baselines              also measure zlib and the sequential riptide decoder
  --output FILE            CSV output (default: standard output)
)";

struct BenchArguments {
    std::vector<std::string> files;
    std::vector<std::size_t> threads;
    std::vector<std::size_t> chunk_sizes{4 * 1024 * 1024};
    std::vector<riptide::WindowStrategy> windows{riptide::WindowStrategy::kSerial,
                                                 riptide::WindowStrategy::kBlocked};
    std::vector<riptide::ChunkingPolicy> chunking{riptide::ChunkingPolicy::kFixed};
    std::vector<riptide::OverflowPolicy> overflow{riptide::OverflowPolicy::kSplit};
    double max_expansion = 20.0;
    std::size_t repetitions = 10;
    std::size_t warmup = 1;
    bool baselines = false;
    std::string output;
};

template <typename T, typename Parse>
std::vector<T> parse_list(std::string_view text, Parse parse) {
    std::vector<T> values;
    for (const auto& item : riptide::cli::split_list(text)) {
        values.push_back(parse(item));
    }
    return values;
}

BenchArguments parse_arguments(int argc, char** argv) {
    namespace cli = riptide::cli;
    BenchArguments args;
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
        } else if (token == "--threads") {
            args.threads = parse_list<std::size_t>(value(), cli::parse_count);
        } else if (token == "--chunk-sizes") {
            args.chunk_sizes = parse_list<std::size_t>(value(), cli::parse_size);
        } else if (token == "--windows") {
            args.windows = parse_list<riptide::WindowStrategy>(value(), cli::parse_window_strategy);
        } else if (token == "--chunking") {
            args.chunking = parse_list<riptide::ChunkingPolicy>(value(), cli::parse_chunking);
        } else if (token == "--overflow") {
            args.overflow = parse_list<riptide::OverflowPolicy>(value(), cli::parse_overflow);
        } else if (token == "--max-expansion") {
            args.max_expansion = cli::parse_number(value());
        } else if (token == "--repetitions") {
            args.repetitions = cli::parse_count(value());
        } else if (token == "--warmup") {
            args.warmup = cli::parse_count(value());
        } else if (token == "--baselines") {
            args.baselines = true;
        } else if (token == "--output") {
            args.output = value();
        } else if (!token.empty() && token.front() == '-') {
            throw std::invalid_argument("unknown option " + std::string(token));
        } else {
            args.files.emplace_back(token);
        }
    }
    if (args.files.empty()) {
        throw std::invalid_argument("no input files given");
    }
    if (args.threads.empty()) {
        const std::size_t hardware = riptide::ThreadPool::hardware_threads();
        for (const std::size_t t : {1, 2, 4, 8}) {
            if (t < hardware) {
                args.threads.push_back(t);
            }
        }
        args.threads.push_back(hardware);
    }
    return args;
}

/// Decompresses every member with zlib; the reference output and the zlib baseline.
std::vector<std::uint8_t> zlib_decompress(std::span<const std::uint8_t> input) {
    std::vector<std::uint8_t> output;
    // The last member's ISIZE field is the exact size of a single-member file, so
    // zlib is not charged for growing its output vector.
    if (input.size() >= 18) {
        const std::uint8_t* isize = input.data() + input.size() - 4;
        output.reserve(isize[0] | isize[1] << 8 | isize[2] << 16 | std::size_t{isize[3]} << 24);
    }
    std::vector<std::uint8_t> buffer(1 << 20);
    z_stream stream{};
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
        throw std::runtime_error("inflateInit2 failed");
    }
    stream.next_in = const_cast<Bytef*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    for (;;) {
        stream.next_out = buffer.data();
        stream.avail_out = static_cast<uInt>(buffer.size());
        const int status = inflate(&stream, Z_NO_FLUSH);
        output.insert(output.end(), buffer.data(), buffer.data() + (buffer.size() - stream.avail_out));
        if (status == Z_STREAM_END) {
            if (stream.avail_in == 0 || stream.next_in[0] != 0x1F) {
                break;
            }
            inflateReset(&stream);  // next gzip member
        } else if (status != Z_OK) {
            inflateEnd(&stream);
            throw std::runtime_error("zlib failed to decompress the input");
        }
    }
    inflateEnd(&stream);
    return output;
}

struct Row {
    std::string file;
    std::string engine;
    std::size_t threads = 1;
    std::size_t chunk_size = 0;
    std::string window = "-";
    std::string chunking = "-";
    std::string overflow = "-";
    std::size_t repetition = 0;
    double seconds = 0.0;
    std::size_t output_bytes = 0;
    bool identical = false;
    std::string statistics = "{}";
};

void write_header(std::ostream& out) {
    out << "file,engine,threads,chunk_size,window,chunking,overflow,repetition,seconds,"
           "output_bytes,throughput_mb_per_s,identical,statistics\n";
}

void write_row(std::ostream& out, const Row& row) {
    const double throughput = row.seconds > 0 ? static_cast<double>(row.output_bytes) / row.seconds / 1e6 : 0;
    std::string json = row.statistics;
    // Quote the JSON so it stays one CSV field.
    std::string quoted = "\"";
    for (const char c : json) {
        quoted += c == '"' ? std::string("\"\"") : std::string(1, c);
    }
    quoted += '"';
    out << row.file << ',' << row.engine << ',' << row.threads << ',' << row.chunk_size << ','
        << row.window << ',' << row.chunking << ',' << row.overflow << ',' << row.repetition << ','
        << row.seconds << ',' << row.output_bytes << ',' << throughput << ','
        << (row.identical ? "true" : "false") << ',' << quoted << '\n';
    out.flush();
}

std::span<const std::uint8_t> view_of(const std::vector<std::uint8_t>& data) { return data; }
std::span<const std::uint8_t> view_of(const riptide::Buffer<std::uint8_t>& data) { return data; }
std::span<const std::uint8_t> view_of(const riptide::DecompressionResult& result) { return result.data(); }

bool same(std::span<const std::uint8_t> a, const std::vector<std::uint8_t>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size()) == 0;
}

std::string base_name(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const BenchArguments args = parse_arguments(argc, argv);
        std::ofstream file_output;
        if (!args.output.empty()) {
            file_output.open(args.output);
        }
        std::ostream& out = args.output.empty() ? std::cout : file_output;
        write_header(out);

        for (const std::string& path : args.files) {
            const riptide::MappedFile input(path);
            const std::vector<std::uint8_t> reference = zlib_decompress(input.bytes());
            std::fprintf(stderr, "%s: %zu -> %zu bytes\n", path.c_str(), input.bytes().size(),
                         reference.size());

            struct Configuration {
                Row row;
                std::function<Row(Row)> run;  ///< One timed run, returning the completed row.
            };
            // Wraps a decompression in timing, the comparison with zlib's output and
            // the decompressor's statistics.
            auto timed = [&](auto&& decompress) {
                return [&reference, decompress](Row row) {
                    const riptide::Stopwatch timer;
                    auto [data, statistics] = decompress();
                    row.seconds = timer.seconds();
                    const auto bytes = view_of(data);
                    row.output_bytes = bytes.size();
                    row.identical = same(bytes, reference);
                    row.statistics = std::move(statistics);
                    return row;
                };
            };

            std::vector<Configuration> configurations;
            if (args.baselines) {
                configurations.push_back({Row{base_name(path), "zlib"}, timed([&input] {
                    return std::pair{zlib_decompress(input.bytes()), std::string("{}")};
                })});
                configurations.push_back({Row{base_name(path), "riptide-sequential"}, timed([&input] {
                    return std::pair{riptide::decompress_sequential(input.bytes()), std::string("{}")};
                })});
            }
            for (const std::size_t threads : args.threads) {
                for (const std::size_t chunk_size : args.chunk_sizes) {
                    for (const auto window : args.windows) {
                        for (const auto chunking : args.chunking) {
                            for (const auto overflow : args.overflow) {
                                riptide::DecompressorOptions options;
                                options.threads = threads;
                                options.chunk_size = chunk_size;
                                options.window_strategy = window;
                                options.chunking = chunking;
                                options.overflow = overflow;
                                options.max_expansion = args.max_expansion;
                                Row row{base_name(path), "riptide", threads, chunk_size,
                                        std::string(riptide::to_string(window)),
                                        std::string(riptide::to_string(chunking)),
                                        std::string(riptide::to_string(overflow))};
                                // The pool is started outside the timed region.
                                auto decompressor =
                                    std::make_shared<riptide::ParallelDecompressor>(options);
                                configurations.push_back({row, timed([&input, decompressor] {
                                    auto result = decompressor->decompress(input.bytes());
                                    std::string statistics = result.statistics.to_json();
                                    return std::pair{std::move(result), std::move(statistics)};
                                })});
                            }
                        }
                    }
                }
            }

            // Repetitions are interleaved across configurations, in alternating
            // order, so that slow drifts of the machine (temperature, background
            // load) affect every configuration alike instead of whichever ran last.
            for (std::size_t r = 0; r < args.warmup; ++r) {
                for (Configuration& configuration : configurations) {
                    static_cast<void>(configuration.run(configuration.row));
                }
            }
            for (std::size_t r = 0; r < args.repetitions; ++r) {
                for (std::size_t i = 0; i < configurations.size(); ++i) {
                    Configuration& configuration =
                        configurations[r % 2 == 0 ? i : configurations.size() - 1 - i];
                    Row row = configuration.run(configuration.row);
                    row.repetition = r;
                    write_row(out, row);
                }
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "riptide_bench: %s\n", error.what());
        return 1;
    }
}
