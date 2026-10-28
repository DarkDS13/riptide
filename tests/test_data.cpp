#include "test_data.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <random>
#include <stdexcept>
#include <string_view>

namespace riptide::test {

Bytes random_bytes(std::size_t size, std::uint32_t seed) {
    std::mt19937 engine(seed);
    Bytes data(size);
    for (auto& byte : data) {
        byte = static_cast<std::uint8_t>(engine());
    }
    return data;
}

Bytes text_like(std::size_t size, std::uint32_t seed) {
    static constexpr std::array<std::string_view, 24> kWords = {
        "the",   "of",       "and",      "parallel", "window", "chunk",  "marker", "scan",
        "block", "huffman",  "deflate",  "thread",   "pool",   "work",   "span",   "prefix",
        "a",     "compress", "decoder",  "stream",   "data",   "is",     "to",     "in"};
    std::mt19937 engine(seed);
    // Zipf-like: index = floor(24 * u^2) favours the first words.
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    Bytes data;
    data.reserve(size + 16);
    while (data.size() < size) {
        const double u = uniform(engine);
        const auto word = kWords[static_cast<std::size_t>(24 * u * u) % kWords.size()];
        data.insert(data.end(), word.begin(), word.end());
        data.push_back(engine() % 11 == 0 ? '\n' : ' ');
    }
    data.resize(size);
    return data;
}

Bytes repetitive(std::size_t size, std::uint32_t seed) {
    std::mt19937 engine(seed);
    Bytes data;
    data.reserve(size);
    while (data.size() < size) {
        const std::size_t period = 1 + engine() % 7;
        const std::size_t run = 1000 + engine() % 50000;
        Bytes pattern(period);
        for (auto& byte : pattern) {
            byte = static_cast<std::uint8_t>('a' + engine() % 26);
        }
        for (std::size_t i = 0; i < run && data.size() < size; ++i) {
            data.push_back(pattern[i % period]);
        }
    }
    return data;
}

Bytes mixed(std::size_t size, std::uint32_t seed) {
    std::mt19937 engine(seed);
    Bytes data;
    data.reserve(size);
    while (data.size() < size) {
        const std::size_t length = std::min<std::size_t>(64 * 1024 + engine() % (512 * 1024), size - data.size());
        Bytes part;
        switch (engine() % 3) {
            case 0:
                part = random_bytes(length, engine());
                break;
            case 1:
                part = text_like(length, engine());
                break;
            default:
                part = repetitive(length, engine());
                break;
        }
        data.insert(data.end(), part.begin(), part.end());
    }
    return data;
}

GzipSettings settings(int level, int strategy, std::size_t flush_every) {
    GzipSettings result;
    result.level = level;
    result.strategy = strategy;
    result.flush_every = flush_every;
    return result;
}

Bytes gzip_compress(std::span<const std::uint8_t> data, const GzipSettings& settings) {
    z_stream stream{};
    if (deflateInit2(&stream, settings.level, Z_DEFLATED, 16 + MAX_WBITS, 8, settings.strategy) != Z_OK) {
        throw std::runtime_error("deflateInit2 failed");
    }
    gz_header header{};
    std::string name = settings.name;
    if (!name.empty()) {
        header.name = reinterpret_cast<Bytef*>(name.data());
        deflateSetHeader(&stream, &header);
    }

    Bytes output(deflateBound(&stream, static_cast<uLong>(data.size())) + 1024 +
                 (settings.flush_every != 0 ? data.size() / settings.flush_every * 16 : 0));
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());

    std::size_t position = 0;
    const std::size_t step = settings.flush_every != 0 ? settings.flush_every : data.size();
    do {
        const std::size_t length = std::min(step, data.size() - position);
        stream.next_in = const_cast<Bytef*>(data.data() + position);
        stream.avail_in = static_cast<uInt>(length);
        position += length;
        const int flush = position == data.size() ? Z_FINISH : Z_FULL_FLUSH;
        const int status = deflate(&stream, flush);
        if (status != Z_OK && status != Z_STREAM_END) {
            deflateEnd(&stream);
            throw std::runtime_error("deflate failed");
        }
    } while (position < data.size());

    output.resize(stream.total_out);
    deflateEnd(&stream);
    return output;
}

Bytes zlib_decompress(std::span<const std::uint8_t> gzip) {
    Bytes output;
    Bytes buffer(1 << 16);
    z_stream stream{};
    inflateInit2(&stream, 16 + MAX_WBITS);
    stream.next_in = const_cast<Bytef*>(gzip.data());
    stream.avail_in = static_cast<uInt>(gzip.size());
    for (;;) {
        stream.next_out = buffer.data();
        stream.avail_out = static_cast<uInt>(buffer.size());
        const int status = inflate(&stream, Z_NO_FLUSH);
        output.insert(output.end(), buffer.data(), buffer.data() + (buffer.size() - stream.avail_out));
        if (status == Z_STREAM_END) {
            if (stream.avail_in == 0 || stream.next_in[0] != 0x1F) {
                break;
            }
            inflateReset(&stream);
        } else if (status != Z_OK) {
            inflateEnd(&stream);
            throw std::runtime_error("zlib rejected the test input");
        }
    }
    inflateEnd(&stream);
    return output;
}

Bytes concatenate(const std::vector<Bytes>& parts) {
    Bytes result;
    for (const auto& part : parts) {
        result.insert(result.end(), part.begin(), part.end());
    }
    return result;
}

}  // namespace riptide::test
