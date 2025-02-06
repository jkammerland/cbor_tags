#pragma once

#include <compare>
#include <cstdint>

namespace cbor::tags {

struct simple {
    using value_type = std::uint8_t;
    value_type value;

    constexpr simple() = default;
    constexpr simple(value_type value) : value(value) {}

    constexpr auto operator<=>(const simple &) const = default;
    constexpr bool operator==(const simple &) const  = default;
};

} // namespace cbor::tags