#include <riptide/gzip/crc32.hpp>

#include <array>
#include <cstring>

#if defined(__ARM_FEATURE_CRC32)
#include <arm_acle.h>
#endif

namespace riptide {
namespace {

constexpr std::uint32_t kPolynomial = 0xEDB88320U;

/// Tables for the slice-by-8 algorithm: table[k][b] is the CRC contribution of
/// byte value b when it is followed by k zero bytes.
constexpr std::array<std::array<std::uint32_t, 256>, 8> make_tables() {
    std::array<std::array<std::uint32_t, 256>, 8> tables{};
    for (std::uint32_t byte = 0; byte < 256; ++byte) {
        std::uint32_t crc = byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) != 0 ? (crc >> 1) ^ kPolynomial : crc >> 1;
        }
        tables[0][byte] = crc;
    }
    for (std::size_t k = 1; k < 8; ++k) {
        for (std::size_t byte = 0; byte < 256; ++byte) {
            const std::uint32_t previous = tables[k - 1][byte];
            tables[k][byte] = (previous >> 8) ^ tables[0][previous & 0xFFU];
        }
    }
    return tables;
}

constexpr auto kTables = make_tables();

/// Multiplies two polynomials modulo the CRC polynomial (reflected representation).
constexpr std::uint32_t multiply_mod_p(std::uint32_t a, std::uint32_t b) noexcept {
    std::uint32_t product = 0;
    for (std::uint32_t mask = 1U << 31; mask != 0; mask >>= 1) {
        if ((a & mask) != 0) {
            product ^= b;
        }
        b = (b & 1U) != 0 ? (b >> 1) ^ kPolynomial : b >> 1;
    }
    return product;
}

/// kPowers[k] = x^(2^k) modulo the CRC polynomial.
constexpr std::array<std::uint32_t, 64> make_powers() {
    std::array<std::uint32_t, 64> powers{};
    std::uint32_t power = 1U << 30;  // x^1
    for (auto& entry : powers) {
        entry = power;
        power = multiply_mod_p(power, power);
    }
    return powers;
}

constexpr auto kPowers = make_powers();

/// x^(8 * byte_count) modulo the CRC polynomial.
std::uint32_t x_to_the_8n(std::size_t byte_count) noexcept {
    std::uint32_t result = 1U << 31;  // x^0
    unsigned k = 3;                   // 8 * n = n * 2^3
    for (std::size_t n = byte_count; n != 0; n >>= 1, ++k) {
        if ((n & 1U) != 0) {
            result = multiply_mod_p(kPowers[k % kPowers.size()], result);
        }
    }
    return result;
}

#if defined(__ARM_FEATURE_CRC32)
/// ARMv8 has CRC-32 instructions for exactly gzip's polynomial; they process eight
/// bytes per instruction, several times faster than the table-driven loop.
std::uint32_t crc32_hardware(std::uint32_t crc, const std::uint8_t* bytes,
                             std::size_t remaining) noexcept {
    while (remaining >= 8) {
        std::uint64_t word = 0;
        std::memcpy(&word, bytes, sizeof(word));
        crc = __crc32d(crc, word);
        bytes += 8;
        remaining -= 8;
    }
    while (remaining-- > 0) {
        crc = __crc32b(crc, *bytes++);
    }
    return crc;
}
#endif

}  // namespace

std::uint32_t crc32_update(std::uint32_t crc, std::span<const std::uint8_t> data) noexcept {
#if defined(__ARM_FEATURE_CRC32)
    return ~crc32_hardware(~crc, data.data(), data.size());
#else
    return crc32_update_portable(crc, data);
#endif
}

std::uint32_t crc32_update_portable(std::uint32_t crc, std::span<const std::uint8_t> data) noexcept {
    crc = ~crc;
    const std::uint8_t* bytes = data.data();
    std::size_t remaining = data.size();

    while (remaining >= 8) {
        std::uint32_t low = 0;
        std::uint32_t high = 0;
        std::memcpy(&low, bytes, 4);
        std::memcpy(&high, bytes + 4, 4);
        low ^= crc;
        crc = kTables[7][low & 0xFFU] ^ kTables[6][(low >> 8) & 0xFFU] ^
              kTables[5][(low >> 16) & 0xFFU] ^ kTables[4][low >> 24] ^
              kTables[3][high & 0xFFU] ^ kTables[2][(high >> 8) & 0xFFU] ^
              kTables[1][(high >> 16) & 0xFFU] ^ kTables[0][high >> 24];
        bytes += 8;
        remaining -= 8;
    }
    while (remaining-- > 0) {
        crc = (crc >> 8) ^ kTables[0][(crc ^ *bytes++) & 0xFFU];
    }
    return ~crc;
}

std::uint32_t crc32_combine(std::uint32_t crc_a, std::uint32_t crc_b,
                            std::size_t length_b) noexcept {
    return multiply_mod_p(x_to_the_8n(length_b), crc_a) ^ crc_b;
}

}  // namespace riptide
