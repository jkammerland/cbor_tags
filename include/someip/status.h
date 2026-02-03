#pragma once

#include <tl/expected.hpp>

#include <cstdint>
#include <string_view>

namespace someip {

enum class status_code : std::uint8_t {
    success = 0,

    buffer_too_small,
    buffer_overrun,
    incomplete_frame,

    invalid_length,
    invalid_protocol_version,
    invalid_interface_version,
    invalid_message_type,
    invalid_return_code,

    invalid_bool_value,
    invalid_utf8,
    invalid_utf16,
    invalid_bom,
    invalid_string_termination,
    invalid_union_selector,

    sd_invalid_header,
    sd_invalid_lengths,
    sd_unknown_option,

    error,
};

constexpr std::string_view status_message(status_code s) noexcept {
    switch (s) {
    case status_code::success: return "success";
    case status_code::buffer_too_small: return "buffer too small";
    case status_code::buffer_overrun: return "buffer overrun";
    case status_code::incomplete_frame: return "incomplete frame";
    case status_code::invalid_length: return "invalid length";
    case status_code::invalid_protocol_version: return "invalid protocol version";
    case status_code::invalid_interface_version: return "invalid interface version";
    case status_code::invalid_message_type: return "invalid message type";
    case status_code::invalid_return_code: return "invalid return code";
    case status_code::invalid_bool_value: return "invalid bool value";
    case status_code::invalid_utf8: return "invalid utf-8";
    case status_code::invalid_utf16: return "invalid utf-16";
    case status_code::invalid_bom: return "invalid BOM";
    case status_code::invalid_string_termination: return "invalid string termination";
    case status_code::invalid_union_selector: return "invalid union selector";
    case status_code::sd_invalid_header: return "invalid SD header";
    case status_code::sd_invalid_lengths: return "invalid SD lengths";
    case status_code::sd_unknown_option: return "unknown SD option";
    case status_code::error: return "error";
    default: return "unknown";
    }
}

template <typename T> using expected = tl::expected<T, status_code>;
template <typename E> using unexpected = tl::unexpected<E>;

} // namespace someip

