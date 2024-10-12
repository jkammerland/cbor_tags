#pragma once
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>

template <typename T> std::string to_hex(const T &bytes) {
    std::string hex;
    hex.reserve(bytes.size() * 2);

    fmt::format_to(std::back_inserter(hex), "{:02x}", fmt::join(bytes, ""));

    return hex;
}