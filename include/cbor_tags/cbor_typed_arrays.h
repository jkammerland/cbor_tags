#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_segments.h"
#include "cbor_tags/detail/cbor_argument.h"

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace cbor::tags {

template <typename T> struct rfc8746_typed_array_traits;

template <> struct rfc8746_typed_array_traits<std::int32_t> {
    using bit_type                     = std::uint32_t;
    static constexpr std::uint64_t tag = 78;
};

template <> struct rfc8746_typed_array_traits<std::int64_t> {
    using bit_type                     = std::uint64_t;
    static constexpr std::uint64_t tag = 79;
};

template <> struct rfc8746_typed_array_traits<double> {
    using bit_type                     = std::uint64_t;
    static constexpr std::uint64_t tag = 86;
};

template <typename T>
concept IsRfc8746TypedArrayElement = requires {
    typename rfc8746_typed_array_traits<std::remove_cv_t<T>>::bit_type;
    { rfc8746_typed_array_traits<std::remove_cv_t<T>>::tag } -> std::convertible_to<std::uint64_t>;
};

namespace detail {

template <IsRfc8746TypedArrayElement T> [[nodiscard]] constexpr auto rfc8746_to_bits(T value) noexcept {
    using value_type = std::remove_cv_t<T>;
    using bit_type   = typename rfc8746_typed_array_traits<value_type>::bit_type;
    return std::bit_cast<bit_type>(value);
}

template <IsRfc8746TypedArrayElement T> [[nodiscard]] constexpr T rfc8746_from_little_endian(std::span<const std::byte> bytes) noexcept {
    using value_type = std::remove_cv_t<T>;
    using bit_type   = typename rfc8746_typed_array_traits<value_type>::bit_type;

    bit_type bits{};
    for (std::size_t i = 0; i < sizeof(value_type); ++i) {
        bits |= static_cast<bit_type>(std::to_integer<std::uint8_t>(bytes[i])) << (i * 8U);
    }
    return std::bit_cast<value_type>(bits);
}

template <IsRfc8746TypedArrayElement T> void append_rfc8746_little_endian_bytes(std::vector<std::byte> &out, T value) {
    using value_type = std::remove_cv_t<T>;
    auto bits        = rfc8746_to_bits(static_cast<value_type>(value));
    for (std::size_t i = 0; i < sizeof(value_type); ++i) {
        out.push_back(static_cast<std::byte>((bits >> (i * 8U)) & 0xFFU));
    }
}

template <IsRfc8746TypedArrayElement T> [[nodiscard]] std::vector<std::byte> rfc8746_little_endian_payload(std::span<const T> values) {
    std::vector<std::byte> bytes;
    bytes.reserve(values.size_bytes());
    for (const auto value : values) {
        append_rfc8746_little_endian_bytes(bytes, value);
    }
    return bytes;
}

} // namespace detail

template <IsRfc8746TypedArrayElement T> class rfc8746_typed_array {
  public:
    using value_type                              = std::remove_cv_t<T>;
    static constexpr std::uint64_t cbor_array_tag = rfc8746_typed_array_traits<value_type>::tag;

    constexpr explicit rfc8746_typed_array(std::span<const value_type> values) noexcept : values_(values) {}

    [[nodiscard]] constexpr std::span<const value_type> values() const noexcept { return values_; }
    [[nodiscard]] std::span<const std::byte>            payload_bytes() const {
        if constexpr (std::endian::native == std::endian::little) {
            return std::as_bytes(values_);
        } else {
            converted_bytes_ = detail::rfc8746_little_endian_payload(values_);
            return converted_bytes_;
        }
    }

    template <typename Encoder> auto encode(Encoder &enc) const { return enc(static_tag<cbor_array_tag>{}, payload_bytes()); }

  private:
    std::span<const value_type>    values_{};
    mutable std::vector<std::byte> converted_bytes_{};
};

template <IsRfc8746TypedArrayElement T> [[nodiscard]] constexpr auto as_rfc8746_typed_array(std::span<const T> values) noexcept {
    return rfc8746_typed_array<std::remove_cv_t<T>>{std::span<const std::remove_cv_t<T>>{values.data(), values.size()}};
}

template <IsRfc8746TypedArrayElement T>
    requires(!std::is_const_v<T>)
[[nodiscard]] constexpr auto as_rfc8746_typed_array(std::span<T> values) noexcept {
    return as_rfc8746_typed_array(std::span<const T>{values.data(), values.size()});
}

template <IsRfc8746TypedArrayElement T, typename Allocator>
[[nodiscard]] constexpr auto as_rfc8746_typed_array(const std::vector<T, Allocator> &values) noexcept {
    return as_rfc8746_typed_array(std::span<const T>{values.data(), values.size()});
}

template <IsRfc8746TypedArrayElement T, typename Allocator> void as_rfc8746_typed_array(std::vector<T, Allocator> &&values) = delete;

template <IsRfc8746TypedArrayElement T> [[nodiscard]] cbor_segments encode_rfc8746_typed_array_segments(std::span<const T> values) {
    if constexpr (std::endian::native == std::endian::little) {
        return encode_tagged_bstr_segments(rfc8746_typed_array_traits<std::remove_cv_t<T>>::tag, std::as_bytes(values));
    } else {
        throw std::logic_error("RFC 8746 typed-array segmented zero-copy encode requires a little-endian native payload; use "
                               "encode_rfc8746_typed_array_segments_copy for converted owned output");
    }
}

template <IsRfc8746TypedArrayElement T>
    requires(!std::is_const_v<T>)
[[nodiscard]] cbor_segments encode_rfc8746_typed_array_segments(std::span<T> values) {
    return encode_rfc8746_typed_array_segments(std::span<const T>{values.data(), values.size()});
}

template <IsRfc8746TypedArrayElement T> [[nodiscard]] cbor_segments encode_rfc8746_typed_array_segments_copy(std::span<const T> values) {
    const auto tag_header =
        detail::encode_cbor_major_argument_header(rfc8746_typed_array_traits<std::remove_cv_t<T>>::tag, std::byte{0xC0});
    auto       payload     = detail::rfc8746_little_endian_payload(values);
    const auto bstr_header = detail::encode_cbor_major_argument_header(payload.size(), std::byte{0x40});

    cbor_segments segments;
    segments.reserve(3);
    segments.append_owned(tag_header.span());
    segments.append_owned(bstr_header.span());
    segments.append_owned(payload);
    return segments;
}

template <IsRfc8746TypedArrayElement T>
    requires(!std::is_const_v<T>)
[[nodiscard]] cbor_segments encode_rfc8746_typed_array_segments_copy(std::span<T> values) {
    return encode_rfc8746_typed_array_segments_copy(std::span<const T>{values.data(), values.size()});
}

template <IsRfc8746TypedArrayElement T> [[nodiscard]] cbor_segments encode_segments(const rfc8746_typed_array<T> &array) {
    return encode_rfc8746_typed_array_segments(array.values());
}

template <IsRfc8746TypedArrayElement T> class rfc8746_typed_array_view {
  public:
    using value_type                              = std::remove_cv_t<T>;
    static constexpr std::uint64_t cbor_array_tag = rfc8746_typed_array_traits<value_type>::tag;

    constexpr rfc8746_typed_array_view() = default;
    constexpr explicit rfc8746_typed_array_view(std::span<const std::byte> payload) noexcept : payload_(payload) {}

    [[nodiscard]] constexpr std::span<const std::byte> payload_bytes() const noexcept { return payload_; }
    [[nodiscard]] constexpr std::size_t                size() const noexcept { return payload_.size() / sizeof(value_type); }

    [[nodiscard]] auto values() const {
        return std::views::iota(std::size_t{}, size()) | std::views::transform([payload = payload_](std::size_t index) {
                   const auto offset = index * sizeof(value_type);
                   return detail::rfc8746_from_little_endian<value_type>(payload.subspan(offset, sizeof(value_type)));
               });
    }

    [[nodiscard]] std::vector<value_type> copy_values() const {
        std::vector<value_type> values;
        values.reserve(size());
        for (auto value : this->values()) {
            values.push_back(value);
        }
        return values;
    }

  private:
    std::span<const std::byte> payload_{};
};

template <IsRfc8746TypedArrayElement T>
[[nodiscard]] expected<rfc8746_typed_array_view<std::remove_cv_t<T>>, status_code>
decode_rfc8746_typed_array_view(std::span<const std::byte> input) {
    using value_type = std::remove_cv_t<T>;
    using view_type  = rfc8746_typed_array_view<value_type>;

    std::size_t offset{};
    if (input.empty()) {
        return unexpected<status_code>(status_code::incomplete);
    }

    const auto tag_initial = std::to_integer<std::uint8_t>(input[offset++]);
    if ((tag_initial >> 5U) != static_cast<std::uint8_t>(major_type::Tag)) {
        return unexpected<status_code>(status_code::no_match_for_tag);
    }

    std::uint64_t tag{};
    auto          status = detail::read_cbor_argument_from_span(input, offset, tag_initial & 0x1FU, tag);
    if (status != status_code::success) {
        return unexpected<status_code>(status);
    }
    if (tag != rfc8746_typed_array_traits<value_type>::tag) {
        return unexpected<status_code>(status_code::no_match_for_tag);
    }

    if (offset >= input.size()) {
        return unexpected<status_code>(status_code::incomplete);
    }
    const auto bstr_initial = std::to_integer<std::uint8_t>(input[offset++]);
    if ((bstr_initial >> 5U) != static_cast<std::uint8_t>(major_type::ByteString) || (bstr_initial & 0x1FU) == 31U) {
        return unexpected<status_code>(status_code::no_match_for_bstr_on_buffer);
    }

    std::uint64_t payload_length{};
    status = detail::read_cbor_argument_from_span(input, offset, bstr_initial & 0x1FU, payload_length);
    if (status != status_code::success) {
        return unexpected<status_code>(status);
    }
    if (payload_length > static_cast<std::uint64_t>(input.size() - offset)) {
        return unexpected<status_code>(status_code::incomplete);
    }
    if ((payload_length % sizeof(value_type)) != 0U) {
        return unexpected<status_code>(status_code::unexpected_group_size);
    }

    const auto payload_size = static_cast<std::size_t>(payload_length);
    auto       payload      = input.subspan(offset, payload_size);
    offset += payload_size;
    if (offset != input.size()) {
        return unexpected<status_code>(status_code::error);
    }

    return view_type{payload};
}

template <IsRfc8746TypedArrayElement T>
[[nodiscard]] expected<rfc8746_typed_array_view<std::remove_cv_t<T>>, status_code>
decode_rfc8746_typed_array(std::span<const std::byte> input) {
    return decode_rfc8746_typed_array_view<T>(input);
}

} // namespace cbor::tags
