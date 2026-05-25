#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_segments.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace cbor::tags::detail {

template <typename Encoder> constexpr void encode_extension_tag_header(Encoder &enc, std::uint64_t tag) {
    append_cbor_major_argument(enc.appender_, enc.data_, tag, cbor_tag_major_byte);
}

template <typename Encoder> constexpr void encode_extension_bstr_header(Encoder &enc, std::uint64_t size) {
    append_cbor_major_argument(enc.appender_, enc.data_, size, cbor_bstr_major_byte);
}

template <typename Encoder>
constexpr void encode_extension_tagged_bstr_header(Encoder &enc, std::uint64_t tag, std::uint64_t payload_size) {
    encode_extension_tag_header(enc, tag);
    encode_extension_bstr_header(enc, payload_size);
}

template <typename R>
concept ExtensionSpanProvider = requires(R &bytes) {
    { bytes.span() } -> std::same_as<std::span<const std::byte>>;
};

template <typename Encoder, typename R>
concept ExtensionAppenderAcceptsSpanProvider =
    ExtensionSpanProvider<R> && requires(Encoder &enc, R &bytes) { enc.appender_.append_owned(enc.data_, bytes.span()); };

template <typename R>
concept ExtensionContiguousSizedByteRange = ByteLikeRange<R> && std::ranges::contiguous_range<R> && std::ranges::sized_range<R>;

template <typename Encoder, typename R>
concept ExtensionAppenderAcceptsByteSpan = ExtensionContiguousSizedByteRange<R> && requires(Encoder &enc, R &&bytes) {
    enc.appender_.append_owned(enc.data_, as_byte_span(std::forward<R>(bytes)));
};

template <typename Encoder, typename R>
    requires ByteLikeRange<R>
constexpr void append_extension_owned_bytes(Encoder &enc, R &&bytes) {
    if constexpr (ExtensionAppenderAcceptsSpanProvider<Encoder, R>) {
        enc.appender_.append_owned(enc.data_, bytes.span());
        return;
    }
    if constexpr (ExtensionAppenderAcceptsByteSpan<Encoder, R>) {
        enc.appender_.append_owned(enc.data_, as_byte_span(std::forward<R>(bytes)));
        return;
    }
    append_byte_range(enc.appender_, enc.data_, std::forward<R>(bytes));
}

template <typename Encoder> constexpr void encode_extension_bstr_payload(Encoder &enc, std::span<const std::byte> payload) {
    encode_extension_bstr_header(enc, static_cast<std::uint64_t>(payload.size()));
    append_extension_owned_bytes(enc, payload);
}

template <typename T>
concept DirectResizableByteOutputBuffer = std::ranges::contiguous_range<T> && requires(T &buffer, std::size_t size) {
    buffer.resize(size);
    { buffer.size() } -> std::convertible_to<std::size_t>;
    std::ranges::data(buffer);
} && sizeof(std::ranges::range_value_t<T>) == 1U;

template <typename Encoder, typename Fill>
constexpr void append_extension_generated_bytes(Encoder &enc, std::size_t byte_count, Fill &&fill) {
    using output_buffer_type = std::remove_reference_t<decltype(enc.data_)>;
    if constexpr (DirectResizableByteOutputBuffer<output_buffer_type>) {
        const auto start = static_cast<std::size_t>(enc.data_.size());
        if (byte_count > (std::numeric_limits<std::size_t>::max)() - start) {
            throw std::length_error("CBOR extension payload exceeds output buffer limits");
        }

        enc.data_.resize(start + byte_count);
        auto *payload = reinterpret_cast<std::byte *>(std::ranges::data(enc.data_) + start);
        std::forward<Fill>(fill)(std::span<std::byte>{payload, byte_count});
    } else {
        std::vector<std::byte> payload(byte_count);
        std::forward<Fill>(fill)(std::span<std::byte>{payload});
        append_extension_owned_bytes(enc, std::span<const std::byte>{payload});
    }
}

template <typename Output, typename Bytes>
concept ExtensionDirectSegmentOutput = requires(Output &output, Bytes bytes) {
    output.append_borrowed(bytes);
    append_owned_segment(output, bytes);
};

template <typename Encoder, typename Segment> constexpr void append_extension_segment(Encoder &enc, const Segment &segment) {
    const auto bytes = segment.bytes();
    if constexpr (ExtensionDirectSegmentOutput<std::remove_reference_t<decltype(enc.data_)>, decltype(bytes)>) {
        if (segment.is_borrowed()) {
            enc.data_.append_borrowed(bytes);
        } else {
            append_owned_segment(enc.data_, bytes);
        }
        return;
    }
    append_segment_to_encoder(enc, segment);
}

template <typename Payload> [[nodiscard]] constexpr std::size_t extension_payload_size(const Payload &payload) noexcept {
    if constexpr (CborSegmentOutputBuffer<std::remove_cvref_t<Payload>>) {
        std::size_t result{};
        for (const auto &segment : payload) {
            result += segment.size();
        }
        return result;
    } else {
        return payload.size();
    }
}

template <typename Encoder, typename Payload> constexpr void append_extension_payload(Encoder &enc, const Payload &payload) {
    if constexpr (CborSegmentOutputBuffer<std::remove_cvref_t<Payload>>) {
        for (const auto &segment : payload) {
            append_extension_segment(enc, segment);
        }
    } else {
        append_extension_owned_bytes(enc, payload);
    }
}

template <typename Encoder, typename EncodePayloadTo, typename EncodeSegments>
[[nodiscard]] constexpr auto make_extension_payload_for_output(Encoder &enc, EncodePayloadTo &&encode_payload_to,
                                                               EncodeSegments &&encode_segments) {
    using output_type = std::remove_reference_t<decltype(enc.data_)>;
    if constexpr (CborAppendOutputBuffer<output_type> && requires { output_type{enc.data_.get_allocator()}; }) {
        output_type           payload{enc.data_.get_allocator()};
        appender<output_type> payload_appender;
        std::forward<EncodePayloadTo>(encode_payload_to)(payload_appender, payload);
        return payload;
    } else {
        return std::forward<EncodeSegments>(encode_segments)();
    }
}

} // namespace cbor::tags::detail
