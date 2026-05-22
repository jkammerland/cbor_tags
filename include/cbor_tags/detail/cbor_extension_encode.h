#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_segments.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <type_traits>

namespace cbor::tags::detail {

template <typename Encoder> constexpr void encode_extension_tag_header(Encoder &enc, std::uint64_t tag) {
    enc.encode_major_and_size(tag, static_cast<typename Encoder::byte_type>(0xC0));
}

template <typename Encoder> constexpr void encode_extension_bstr_header(Encoder &enc, std::uint64_t size) {
    enc.encode_major_and_size(size, static_cast<typename Encoder::byte_type>(0x40));
}

template <typename Encoder, typename R>
    requires ByteLikeRange<R>
constexpr void append_extension_owned_bytes(Encoder &enc, R &&bytes) {
    if constexpr (requires {
                      { bytes.span() } -> std::same_as<std::span<const std::byte>>;
                      enc.appender_.append_owned(enc.data_, bytes.span());
                  }) {
        enc.appender_.append_owned(enc.data_, bytes.span());
    } else if constexpr (std::ranges::contiguous_range<R> && std::ranges::sized_range<R> &&
                         requires { enc.appender_.append_owned(enc.data_, as_byte_span(std::forward<R>(bytes))); }) {
        enc.appender_.append_owned(enc.data_, as_byte_span(std::forward<R>(bytes)));
    } else {
        append_byte_range(enc.appender_, enc.data_, std::forward<R>(bytes));
    }
}

template <typename Encoder> constexpr void encode_extension_bstr_payload(Encoder &enc, std::span<const std::byte> payload) {
    encode_extension_bstr_header(enc, static_cast<std::uint64_t>(payload.size()));
    append_extension_owned_bytes(enc, payload);
}

template <typename Encoder, typename Segment> constexpr void append_extension_segment(Encoder &enc, const Segment &segment) {
    const auto bytes = segment.bytes();
    if constexpr (requires {
                      enc.data_.append_borrowed(bytes);
                      append_owned_segment(enc.data_, bytes);
                  }) {
        if (segment.is_borrowed()) {
            enc.data_.append_borrowed(bytes);
        } else {
            append_owned_segment(enc.data_, bytes);
        }
    } else {
        append_segment_to_encoder(enc, segment);
    }
}

} // namespace cbor::tags::detail
