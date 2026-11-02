#pragma once

#include <concepts>
#include <cstddef>
#include <ranges>
#include <string>

namespace example {

// Format byte ranges, including segmented output, without prewalking the bytes.
template <std::ranges::input_range Bytes>
    requires std::same_as<std::ranges::range_value_t<Bytes>, std::byte>
std::string to_hex(Bytes &&bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;

    for (std::byte byte : bytes) {
        const auto value = std::to_integer<unsigned>(byte);
        result.push_back(digits[value >> 4]);
        result.push_back(digits[value & 0x0f]);
    }

    return result;
}

} // namespace example
