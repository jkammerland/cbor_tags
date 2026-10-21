#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/detail/cbor_extension_decode.h"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

namespace cbor::tags::detail {

// Associative insertion cannot preserve a typed array's order and duplicates.
template <typename R>
concept TypedArraySequenceRange = std::ranges::forward_range<R> && (!requires { typename std::remove_cvref_t<R>::key_type; });

template <typename R>
concept TypedArrayWritableRange =
    TypedArraySequenceRange<R> && std::same_as<std::ranges::range_reference_t<R>, std::ranges::range_value_t<R> &>;

template <typename R>
concept TypedArrayBackAppendableRange = requires(R &range, std::ranges::range_value_t<R> value) {
    range.clear();
    range.push_back(value);
};

template <typename R>
concept TypedArrayAfterAppendableRange = requires(R &range, std::ranges::range_value_t<R> value) {
    range.clear();
    range.before_begin();
    { range.insert_after(range.before_begin(), value) } -> std::same_as<decltype(range.before_begin())>;
};

template <typename R>
concept TypedArrayAppendableRange = TypedArrayBackAppendableRange<R> || TypedArrayAfterAppendableRange<R>;

template <typename R>
concept TypedArrayDestinationRange = TypedArrayWritableRange<R>;

template <typename R> [[nodiscard]] constexpr std::uint64_t typed_array_range_size(R &range) {
    return static_cast<std::uint64_t>(std::ranges::distance(range));
}

// Validate the declared destination extent before consuming or changing values.
template <TypedArrayDestinationRange R> [[nodiscard]] status_code typed_array_destination_size_status(R &range, std::uint64_t count) {
    if constexpr (TypedArrayAppendableRange<R>) {
        if constexpr (requires { range.max_size(); }) {
            if (std::cmp_greater(count, range.max_size())) {
                return status_code::size_limit_exceeded;
            }
        }
        // Full/capacity sequences can overwrite their oldest elements on
        // push_back. Reject excess values before touching a circular buffer.
        if constexpr (requires {
                          { range.full() } -> std::convertible_to<bool>;
                          static_cast<std::uint64_t>(range.capacity());
                      }) {
            if (count > static_cast<std::uint64_t>(range.capacity())) {
                return status_code::size_limit_exceeded;
            }
        }
    } else if (count != typed_array_range_size(range)) {
        return status_code::unexpected_group_size;
    }
    return status_code::success;
}

template <bool NativeEndian, typename Decoder, TypedArrayDestinationRange R, typename DecodeValue>
[[nodiscard]] status_code decode_typed_array_range(Decoder &dec, R &range, std::uint64_t byte_count, DecodeValue decode_value) {
    using value_type  = std::ranges::range_value_t<R>;
    const auto count  = byte_count / sizeof(value_type);
    auto       status = typed_array_destination_size_status(range, count);
    if (status != status_code::success) {
        return status;
    }

    status = require_extension_payload_bytes(dec, byte_count);
    if (status != status_code::success) {
        return status;
    }

    // A complete contiguous input can be copied straight into existing storage.
    constexpr bool resize_contiguous = std::ranges::contiguous_range<R> && requires { range.resize(std::size_t{}); };
    if constexpr (IsContiguous<typename Decoder::input_buffer_type> && std::ranges::contiguous_range<R> &&
                  (!TypedArrayAppendableRange<R> || resize_contiguous)) {
        if constexpr (TypedArrayAppendableRange<R>) {
            range.resize(static_cast<std::size_t>(count));
        }
        return consume_extension_bstring_payload(dec, byte_count, [&](auto &&payload) {
            if (byte_count == 0U) {
                return status_code::success;
            }
            const auto *bytes = reinterpret_cast<const std::byte *>(std::ranges::data(payload));
            if constexpr (NativeEndian) {
                std::memcpy(std::ranges::data(range), bytes, static_cast<std::size_t>(byte_count));
            } else {
                for (auto &value : range) {
                    value = decode_value(std::span<const std::byte>{bytes, sizeof(value_type)});
                    bytes += sizeof(value_type);
                }
            }
            return status_code::success;
        });
    } else {
        if constexpr (TypedArrayAppendableRange<R>) {
            // Unsized input must not trigger allocation from its header alone.
            if constexpr (IsContiguous<typename Decoder::input_buffer_type> ||
                          std::ranges::sized_range<const typename Decoder::input_buffer_type>) {
                if constexpr (requires { range.reserve(std::size_t{}); }) {
                    range.reserve(static_cast<std::size_t>(count));
                }
            }
            range.clear();
        }

        auto consume_values = [&](auto write_value) {
            for (std::uint64_t index = 0; index < count; ++index) {
                // Commit only complete elements; a truncated element consumes
                // its available bytes without writing a destination element.
                std::array<std::byte, sizeof(value_type)> bytes{};
                for (auto &byte : bytes) {
                    if (decoder_at_end(dec)) {
                        return status_code::incomplete;
                    }
                    byte = static_cast<std::byte>(dec.read_uint8());
                }
                write_value(decode_value(std::span<const std::byte>{bytes}));
            }
            return status_code::success;
        };

        if constexpr (TypedArrayBackAppendableRange<R>) {
            return consume_values([&](value_type value) { range.push_back(value); });
        } else if constexpr (TypedArrayAfterAppendableRange<R>) {
            auto cursor = range.before_begin();
            return consume_values([&](value_type value) { cursor = range.insert_after(cursor, value); });
        } else {
            auto cursor = std::ranges::begin(range);
            return consume_values([&](value_type value) {
                *cursor = value;
                ++cursor;
            });
        }
    }
}

} // namespace cbor::tags::detail
