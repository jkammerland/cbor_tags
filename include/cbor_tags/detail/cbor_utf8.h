#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <type_traits>

namespace cbor::tags::detail {

struct utf8_prefix {
    std::uint8_t first{};
    std::uint8_t second{};
};

template <typename T> [[nodiscard]] constexpr std::uint8_t utf8_byte(T value) noexcept {
    if constexpr (std::same_as<std::remove_cvref_t<T>, std::byte>) {
        return std::to_integer<std::uint8_t>(value);
    } else {
        return static_cast<std::uint8_t>(static_cast<unsigned char>(value));
    }
}

[[nodiscard]] constexpr bool is_utf8_continuation_byte(std::uint8_t value) noexcept { return (value & 0xC0U) == 0x80U; }

[[nodiscard]] constexpr std::size_t utf8_sequence_length(std::uint8_t value) noexcept {
    if (value <= 0x7FU) {
        return 1U;
    }
    if (value >= 0xC2U && value <= 0xDFU) {
        return 2U;
    }
    if (value >= 0xE0U && value <= 0xEFU) {
        return 3U;
    }
    if (value >= 0xF0U && value <= 0xF4U) {
        return 4U;
    }
    return 0U;
}

[[nodiscard]] constexpr bool is_valid_utf8_second_byte(utf8_prefix prefix) noexcept {
    return !((prefix.first == 0xE0U && prefix.second < 0xA0U) || (prefix.first == 0xEDU && prefix.second > 0x9FU) ||
             (prefix.first == 0xF0U && prefix.second < 0x90U) || (prefix.first == 0xF4U && prefix.second > 0x8FU));
}

template <typename Iterator, typename Sentinel> [[nodiscard]] bool is_valid_utf8(Iterator begin, const Sentinel &end) {
    while (begin != end) {
        std::array<std::uint8_t, 4> sequence{};
        sequence[0] = utf8_byte(*begin);
        ++begin;

        const auto sequence_length = utf8_sequence_length(sequence[0]);
        if (sequence_length == 0U) {
            return false;
        }

        for (std::size_t offset = 1U; offset < sequence_length; ++offset) {
            if (begin == end) {
                return false;
            }
            sequence[offset] = utf8_byte(*begin);
            ++begin;
            if (!is_utf8_continuation_byte(sequence[offset])) {
                return false;
            }
        }

        if (sequence_length > 1U && !is_valid_utf8_second_byte({.first = sequence[0], .second = sequence[1]})) {
            return false;
        }
    }
    return true;
}

template <std::ranges::input_range Range> [[nodiscard]] bool is_valid_utf8(Range &&range) {
    return is_valid_utf8(std::ranges::begin(range), std::ranges::end(range));
}

} // namespace cbor::tags::detail
