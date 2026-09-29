#pragma once

#include "cbor_tags/cbor.h"

#include <stdexcept>
#include <string>

namespace cbor::tags::detail {

// Value-returning reader primitives preserve their parse status until the
// public decoder/traversal boundary converts it to the configured result type.
// Preserve the runtime_error boundary used by direct readers and visualization.
struct decode_status_exception : std::runtime_error {
    status_code status;

    explicit decode_status_exception(status_code value) : std::runtime_error(std::string{status_message(value)}), status(value) {}
};

[[nodiscard]] constexpr status_code major_type_mismatch_status(major_type major) noexcept {
    switch (major) {
    case major_type::UnsignedInteger: return status_code::no_match_for_uint_on_buffer;
    case major_type::NegativeInteger: return status_code::no_match_for_nint_on_buffer;
    case major_type::ByteString: return status_code::no_match_for_bstr_on_buffer;
    case major_type::TextString: return status_code::no_match_for_tstr_on_buffer;
    case major_type::Array: return status_code::no_match_for_array_on_buffer;
    case major_type::Map: return status_code::no_match_for_map_on_buffer;
    case major_type::Tag: return status_code::no_match_for_tag_on_buffer;
    case major_type::Simple: return status_code::no_match_for_simple_on_buffer;
    }
    return status_code::unsupported_operation;
}

} // namespace cbor::tags::detail
