#pragma once

#include <concepts>
#include <cstddef>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

template <typename Byte = std::byte>
    requires(sizeof(Byte) == 1)
inline std::vector<Byte> to_bytes(std::string_view hex) {
    if (hex.length() % 2 != 0) {
        throw std::invalid_argument("Hex can only be even in length");
    }

    std::vector<Byte> bytes;
    bytes.reserve(hex.length() / 2);

    auto byte_to_int = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        throw std::invalid_argument("Invalid hex character");
    };

    for (std::size_t i = 0; i < hex.length(); i += 2) {
        int high = byte_to_int(hex[i]);
        int low = byte_to_int(hex[i + 1]);
        bytes.push_back(static_cast<Byte>((high << 4) | low));
    }

    return bytes;
}

} // namespace example
