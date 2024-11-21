#pragma once
#include <cstddef>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>

template <typename T> inline std::string to_hex(const T &bytes) {
    std::string hex;
    hex.reserve(bytes.size() * 2);

    fmt::format_to(std::back_inserter(hex), "{:02x}", fmt::join(bytes, ""));

    return hex;
}

template <typename byte = std::byte>
    requires(sizeof(byte) == 1)
inline std::vector<byte> to_bytes(std::string_view hex) {
    if (hex.length() % 2 != 0) {
        return {};
    }

    std::vector<byte> bytes;
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

    for (size_t i = 0; i < hex.length(); i += 2) {
        int high = byte_to_int(hex[i]);
        int low  = byte_to_int(hex[i + 1]);
        bytes.push_back(static_cast<byte>((high << 4) | low));
    }

    return bytes;
}

// Print using fmt
inline auto print_bytes = [](const std::vector<std::byte> &bytes) { fmt::print("{}\n", to_hex(bytes)); };