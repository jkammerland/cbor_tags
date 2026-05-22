#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/detail/cbor_argument.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace cbor::tags::detail {

template <typename Decoder>
[[nodiscard]] constexpr status_code read_extension_initial_byte(Decoder &dec, major_type &major, std::byte &additional_info) {
    if (dec.reader_.empty(dec.data_)) {
        return status_code::incomplete;
    }

    auto header     = dec.read_initial_byte();
    major           = header.first;
    additional_info = header.second;
    return status_code::success;
}

template <typename Decoder>
[[nodiscard]] constexpr status_code decode_unsigned_argument(Decoder &dec, std::byte additional_info, std::uint64_t &value) {
    const auto info = std::to_integer<std::uint8_t>(additional_info);
    if (!is_valid_cbor_argument_info(info)) {
        return status_code::error;
    }

    const auto payload_size = cbor_argument_payload_size(info);
    if (payload_size > 0U && dec.reader_.empty(dec.data_, static_cast<typename Decoder::size_type>(payload_size - 1U))) {
        return status_code::incomplete;
    }

    value = dec.decode_unsigned(additional_info);
    return status_code::success;
}

template <typename Decoder>
[[nodiscard]] constexpr status_code decode_definite_array_size(Decoder &dec, major_type major, std::byte additional_info,
                                                               std::uint64_t &size) {
    if (major != major_type::Array) {
        return status_code::no_match_for_array_on_buffer;
    }
    if (additional_info == std::byte{31}) {
        return status_code::unexpected_group_size;
    }
    return decode_unsigned_argument(dec, additional_info, size);
}

template <typename Decoder> [[nodiscard]] constexpr status_code require_extension_payload_bytes(Decoder &dec, std::uint64_t byte_count) {
    if constexpr (std::numeric_limits<typename Decoder::size_type>::max() < std::numeric_limits<std::uint64_t>::max()) {
        if (byte_count > static_cast<std::uint64_t>(std::numeric_limits<typename Decoder::size_type>::max())) {
            return status_code::error;
        }
    }
    if (byte_count > 0U && dec.reader_.empty(dec.data_, static_cast<typename Decoder::size_type>(byte_count - 1U))) {
        return status_code::incomplete;
    }
    return status_code::success;
}

[[nodiscard]] constexpr status_code match_expected_tag(std::uint64_t expected_tag, std::uint64_t actual_tag) {
    if (actual_tag != expected_tag) {
        return status_code::no_match_for_tag;
    }
    return status_code::success;
}

template <typename Fn>
[[nodiscard]] constexpr status_code decode_tagged_payload(std::uint64_t expected_tag, std::uint64_t actual_tag, Fn &&decode_payload) {
    const auto tag_status = match_expected_tag(expected_tag, actual_tag);
    if (tag_status != status_code::success) {
        return tag_status;
    }
    return std::forward<Fn>(decode_payload)();
}

template <typename Decoder, typename Fn>
[[nodiscard]] constexpr status_code decode_tagged_payload(Decoder &dec, std::uint64_t expected_tag, major_type major,
                                                          std::byte additional_info, Fn &&decode_payload) {
    if (major != major_type::Tag) {
        return status_code::no_match_for_tag_on_buffer;
    }

    std::uint64_t actual_tag{};
    auto          status = decode_unsigned_argument(dec, additional_info, actual_tag);
    if (status != status_code::success) {
        return status;
    }

    return decode_tagged_payload(expected_tag, actual_tag, std::forward<Fn>(decode_payload));
}

template <typename Decoder, typename Fn>
[[nodiscard]] constexpr status_code decode_tagged_payload_header(Decoder &dec, std::uint64_t expected_tag, std::uint64_t actual_tag,
                                                                 Fn &&decode_payload) {
    const auto tag_status = match_expected_tag(expected_tag, actual_tag);
    if (tag_status != status_code::success) {
        return tag_status;
    }

    major_type payload_major{};
    std::byte  payload_info{};
    auto       status = read_extension_initial_byte(dec, payload_major, payload_info);
    if (status != status_code::success) {
        return status;
    }
    return std::forward<Fn>(decode_payload)(payload_major, payload_info);
}

template <typename Decoder, typename Fn>
[[nodiscard]] constexpr status_code decode_tagged_payload_header(Decoder &dec, std::uint64_t expected_tag, major_type major,
                                                                 std::byte additional_info, Fn &&decode_payload) {
    if (major != major_type::Tag) {
        return status_code::no_match_for_tag_on_buffer;
    }

    std::uint64_t actual_tag{};
    auto          status = decode_unsigned_argument(dec, additional_info, actual_tag);
    if (status != status_code::success) {
        return status;
    }
    return decode_tagged_payload_header(dec, expected_tag, actual_tag, std::forward<Fn>(decode_payload));
}

template <typename Fn>
[[nodiscard]] constexpr status_code decode_bstr_payload_header(major_type payload_major, std::byte payload_info, Fn &&decode_payload) {
    if (payload_major != major_type::ByteString || payload_info == std::byte{31}) {
        return status_code::no_match_for_bstr_on_buffer;
    }
    return std::forward<Fn>(decode_payload)(payload_info);
}

template <typename Decoder, typename Fn>
[[nodiscard]] constexpr status_code decode_tagged_bstr_payload_header(Decoder &dec, std::uint64_t expected_tag, std::uint64_t actual_tag,
                                                                      Fn &&decode_payload) {
    return decode_tagged_payload_header(dec, expected_tag, actual_tag, [&](major_type payload_major, std::byte payload_info) {
        return decode_bstr_payload_header(payload_major, payload_info, std::forward<Fn>(decode_payload));
    });
}

template <typename Decoder, typename Fn>
[[nodiscard]] constexpr status_code decode_tagged_bstr_payload_header(Decoder &dec, std::uint64_t expected_tag, major_type major,
                                                                      std::byte additional_info, Fn &&decode_payload) {
    if (major != major_type::Tag) {
        return status_code::no_match_for_tag_on_buffer;
    }

    std::uint64_t actual_tag{};
    auto          status = decode_unsigned_argument(dec, additional_info, actual_tag);
    if (status != status_code::success) {
        return status;
    }
    return decode_tagged_bstr_payload_header(dec, expected_tag, actual_tag, std::forward<Fn>(decode_payload));
}

} // namespace cbor::tags::detail
