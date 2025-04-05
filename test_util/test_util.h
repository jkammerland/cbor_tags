#pragma once
#include "cbor_tags/cbor.h"
#include "small_generator.h"

#include <cstddef>
#include <doctest/doctest.h>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <tl/expected.hpp>

template <typename T> inline std::string to_hex(const T &bytes) {
    std::string hex;

    if constexpr (std::is_integral_v<std::decay_t<T>> || std::is_same_v<std::decay_t<T>, std::byte>) {
        // Handle single byte/integral value
        unsigned char value;
        if constexpr (std::is_same_v<std::decay_t<T>, std::byte>) {
            value = std::to_integer<unsigned char>(bytes);
        } else {
            value = static_cast<unsigned char>(bytes);
        }
        fmt::format_to(std::back_inserter(hex), "{:02x}", value);
    } else if constexpr (std::ranges::range<T>) {
        // Handle any range (containers and views)
        hex.reserve(std::distance(std::begin(bytes), std::end(bytes)) * 2);
        for (const auto &byte : bytes) {
            hex += to_hex(byte);
        }
    }

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

template <std::ranges::range Buffer, typename... Strings>
tl::expected<void, std::vector<std::string>> substrings_in(Buffer &&buffer, Strings &&...strings) {
    // Convert buffer to string for non-contiguous ranges support
    using buffer_type = std::conditional_t<std::ranges::contiguous_range<Buffer>, std::string_view, std::string>;
    buffer_type buffer_str;
    if constexpr (std::ranges::contiguous_range<Buffer>) {
        buffer_str = std::string_view{std::ranges::data(buffer), std::ranges::size(buffer)};
    } else {
        buffer_str.assign(std::ranges::begin(buffer), std::ranges::end(buffer));
    }

    // Vector to store not found strings
    std::vector<std::string> not_found;

    // Helper function to check each string
    auto check_string = [&](const auto &str) {
        // Convert input to string_view for consistent searching
        std::string_view sv;
        if constexpr (std::is_convertible_v<decltype(str), std::string_view>) {
            sv = str;
        } else {
            sv = std::string_view{str};
        }

        if (buffer_str.find(sv) == std::string::npos) {
            not_found.emplace_back(sv);
        }
    };

    // Check all strings
    (check_string(std::forward<Strings>(strings)), ...);

    // Return result
    if (not_found.empty()) {
        return {}; // All strings found
    } else {
        return tl::unexpected(std::move(not_found));
    }
}

namespace doctest {
template <typename T> struct StringMaker<tl::expected<T, std::vector<std::string>>> {
    static String convert(const tl::expected<T, std::vector<std::string>> &value) {
        if (value.has_value()) {
            return "expected(has_value)";
        } else {
            std::string result = "unexpected(";
            const auto &errors = value.error();
            for (size_t i = 0; i < errors.size(); ++i) {
                result += '"' + errors[i] + '"';
                if (i < errors.size() - 1) {
                    result += ", ";
                }
            }
            result += ")";
            return result.c_str();
        }
    }
};

template <typename T> struct StringMaker<tl::expected<T, cbor::tags::status_code>> {
    static String convert(const tl::expected<T, cbor::tags::status_code> &value) {
        if (value.has_value()) {
            return "expected(has_value)";
        } else {
            return String(fmt::format("unexpected({})", cbor::tags::status_message(value.error())).c_str());
        }
    }
};

} // namespace doctest