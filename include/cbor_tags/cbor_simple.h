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

[[nodiscard]] constexpr bool is_valid_simple_value(simple::value_type value) noexcept {
    return value < static_cast<simple::value_type>(24U) || value > static_cast<simple::value_type>(31U);
}

} // namespace cbor::tags
