#pragma once

#include "cbor_tags/cbor_segments.h"
#include "cbor_tags/codec/cwt.h"

#include <concepts>
#include <ranges>
#include <span>
#include <type_traits>

namespace cbor::tags::cwt::detail {

template <typename Parts>
concept SignatureParts =
    std::ranges::input_range<Parts> && std::same_as<std::remove_cvref_t<std::ranges::range_reference_t<Parts>>, std::span<const std::byte>>;

// Expose byte spans without exposing the segment storage policy to the backend.
inline auto signature_parts(const byte_segments &segments) {
    return segments | std::views::transform([](const auto &segment) { return segment.bytes(); });
}

inline expected<byte_segments, status_code> encode_signature_input(const sig_structure_view &structure) {
    byte_segments segments;

    auto encoder = make_encoder<codec::cwt>(segments);
    auto result  = encoder(structure);
    if (!result) {
        return unexpected<status_code>{result.error()};
    }
    return segments;
}

} // namespace cbor::tags::cwt::detail
