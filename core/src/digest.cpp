#include "digest.hpp"

#include <cstddef>

namespace odysea::core::detail {
namespace {

constexpr std::size_t block_bytes = 64;
constexpr std::size_t length_bytes = 8;
constexpr std::size_t digest_bytes = 16;
constexpr std::size_t word_count = 16;
constexpr std::size_t round_count = 64;

// Round constants: the integer part of 2^32 * abs(sin(step + 1)), as specified
// by RFC 1321. Reproduced rather than computed so the table cannot drift with
// the floating-point behavior of a particular target.
constexpr std::array<std::uint32_t, round_count> round_constants{
    0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU, 0xf57c0fafU, 0x4787c62aU, 0xa8304613U,
    0xfd469501U, 0x698098d8U, 0x8b44f7afU, 0xffff5bb1U, 0x895cd7beU, 0x6b901122U, 0xfd987193U,
    0xa679438eU, 0x49b40821U, 0xf61e2562U, 0xc040b340U, 0x265e5a51U, 0xe9b6c7aaU, 0xd62f105dU,
    0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U, 0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU,
    0xa9e3e905U, 0xfcefa3f8U, 0x676f02d9U, 0x8d2a4c8aU, 0xfffa3942U, 0x8771f681U, 0x6d9d6122U,
    0xfde5380cU, 0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U, 0x289b7ec6U, 0xeaa127faU,
    0xd4ef3085U, 0x04881d05U, 0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U, 0xf4292244U,
    0x432aff97U, 0xab9423a7U, 0xfc93a039U, 0x655b59c3U, 0x8f0ccc92U, 0xffeff47dU, 0x85845dd1U,
    0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U, 0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU,
    0xeb86d391U};

/// Per-step left-rotation amounts, four repeating values per round.
constexpr std::array<std::uint32_t, round_count> rotations{
    7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U,
    5U, 9U,  14U, 20U, 5U, 9U,  14U, 20U, 5U, 9U,  14U, 20U, 5U, 9U,  14U, 20U,
    4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U,
    6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U};

constexpr std::array<std::uint32_t, 4> initial_state{0x67452301U, 0xefcdab89U, 0x98badcfeU,
                                                     0x10325476U};

using Block = std::array<std::uint8_t, block_bytes>;
using State = std::array<std::uint32_t, 4>;

constexpr std::uint32_t rotate_left(std::uint32_t value, std::uint32_t count) noexcept {
    return (value << count) | (value >> (32U - count));
}

/// Mixing function and message-word selector for one step, as specified by the
/// four rounds of RFC 1321.
struct Step {
    std::uint32_t mixed = 0;
    std::size_t word = 0;
};

constexpr Step step_for(std::size_t index, std::uint32_t b, std::uint32_t c,
                        std::uint32_t d) noexcept {
    if (index < 16) {
        return {.mixed = (b & c) | (~b & d), .word = index};
    }
    if (index < 32) {
        return {.mixed = (d & b) | (~d & c), .word = ((5 * index) + 1) % word_count};
    }
    if (index < 48) {
        return {.mixed = b ^ c ^ d, .word = ((3 * index) + 5) % word_count};
    }
    return {.mixed = c ^ (b | ~d), .word = (7 * index) % word_count};
}

void transform(State& state, const Block& block) {
    std::array<std::uint32_t, word_count> words{};
    for (std::size_t index = 0; index < word_count; ++index) {
        const std::size_t base = index * 4;
        words.at(index) = static_cast<std::uint32_t>(block.at(base)) |
                          (static_cast<std::uint32_t>(block.at(base + 1)) << 8U) |
                          (static_cast<std::uint32_t>(block.at(base + 2)) << 16U) |
                          (static_cast<std::uint32_t>(block.at(base + 3)) << 24U);
    }

    std::uint32_t a = state.at(0);
    std::uint32_t b = state.at(1);
    std::uint32_t c = state.at(2);
    std::uint32_t d = state.at(3);

    for (std::size_t index = 0; index < round_count; ++index) {
        const Step step = step_for(index, b, c, d);
        const std::uint32_t sum = a + step.mixed + round_constants.at(index) + words.at(step.word);
        a = d;
        d = c;
        c = b;
        b += rotate_left(sum, rotations.at(index));
    }

    state.at(0) += a;
    state.at(1) += b;
    state.at(2) += c;
    state.at(3) += d;
}

} // namespace

std::array<std::uint8_t, digest_bytes> md5(std::string_view data) {
    State state = initial_state;
    Block block{};

    std::size_t offset = 0;
    while (data.size() - offset >= block_bytes) {
        for (std::size_t index = 0; index < block_bytes; ++index) {
            block.at(index) = static_cast<std::uint8_t>(data[offset + index]);
        }
        transform(state, block);
        offset += block_bytes;
    }

    const std::size_t remainder = data.size() - offset;
    block.fill(0);
    for (std::size_t index = 0; index < remainder; ++index) {
        block.at(index) = static_cast<std::uint8_t>(data[offset + index]);
    }
    block.at(remainder) = 0x80U;

    // The length field needs the final eight bytes. When the padded remainder
    // leaves no room, this block is flushed and the length lands in the next.
    if (remainder + 1 > block_bytes - length_bytes) {
        transform(state, block);
        block.fill(0);
    }

    const std::uint64_t bit_length = static_cast<std::uint64_t>(data.size()) * 8U;
    for (std::size_t index = 0; index < length_bytes; ++index) {
        const auto shift = static_cast<std::uint32_t>(index * 8);
        block.at(block_bytes - length_bytes + index) =
            static_cast<std::uint8_t>((bit_length >> shift) & 0xFFU);
    }
    transform(state, block);

    std::array<std::uint8_t, digest_bytes> digest{};
    for (std::size_t word = 0; word < state.size(); ++word) {
        for (std::size_t byte = 0; byte < 4; ++byte) {
            const auto shift = static_cast<std::uint32_t>(byte * 8);
            digest.at((word * 4) + byte) =
                static_cast<std::uint8_t>((state.at(word) >> shift) & 0xFFU);
        }
    }
    return digest;
}

std::string md5_hex(std::string_view data) {
    constexpr std::string_view hex_digits = "0123456789abcdef";
    const std::array<std::uint8_t, digest_bytes> digest = md5(data);

    std::string text;
    text.reserve(digest.size() * 2);
    for (const std::uint8_t value : digest) {
        text.push_back(hex_digits[value >> 4U]);
        text.push_back(hex_digits[value & 0x0FU]);
    }
    return text;
}

} // namespace odysea::core::detail
