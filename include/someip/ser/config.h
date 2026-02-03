#pragma once

#include "someip/wire/endian.h"

#include <cstddef>

namespace someip::ser {

struct config {
    wire::endian payload_endian;
    std::byte    pad_byte{std::byte{0x00}};

    explicit constexpr config(wire::endian payload_endian_, std::byte pad_byte_ = std::byte{0x00}) noexcept
        : payload_endian(payload_endian_), pad_byte(pad_byte_) {}
};

} // namespace someip::ser

