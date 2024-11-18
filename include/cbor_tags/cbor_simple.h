#pragma once

#include <compare>
#include <cstdint>

namespace cbor::tags {

struct simple {
    std::uint8_t value;

    constexpr simple() = default;
    constexpr simple(std::uint8_t value) : value(value) {}

    constexpr auto operator<=>(const simple &) const = default;
    constexpr bool operator==(const simple &) const  = default;
};

} // namespace cbor::tags