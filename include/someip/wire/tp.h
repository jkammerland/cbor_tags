#pragma once

#include "someip/status.h"
#include "someip/wire/cursor.h"
#include "someip/wire/endian.h"

#include <cstdint>
#include <optional>

namespace someip::wire {

struct tp_header {
    std::uint32_t offset_units_16B{0}; // 28 bits
    std::uint8_t  reserved{0};         // 3 bits
    bool          more_segments{false}; // 1 bit
};

[[nodiscard]] constexpr std::uint32_t pack_tp_header(tp_header tp) noexcept {
    const auto offset = tp.offset_units_16B & 0x0FFFFFFFu;
    const auto rsv    = std::uint32_t(tp.reserved & 0x7u);
    const auto m      = tp.more_segments ? 1u : 0u;
    return (offset << 4) | (rsv << 1) | m;
}

[[nodiscard]] constexpr tp_header unpack_tp_header(std::uint32_t v) noexcept {
    tp_header tp{};
    tp.offset_units_16B = (v >> 4) & 0x0FFFFFFFu;
    tp.reserved         = static_cast<std::uint8_t>((v >> 1) & 0x7u);
    tp.more_segments    = (v & 0x1u) != 0u;
    return tp;
}

template <class Out>
expected<void> encode_tp_header(writer<Out> &out, const tp_header &tp) noexcept {
    return write_uint<endian::big>(out, pack_tp_header(tp));
}

inline expected<tp_header> decode_tp_header(reader &in) noexcept {
    auto v = read_uint<endian::big, std::uint32_t>(in);
    if (!v) {
        return unexpected<status_code>(v.error());
    }
    return unpack_tp_header(*v);
}

} // namespace someip::wire

