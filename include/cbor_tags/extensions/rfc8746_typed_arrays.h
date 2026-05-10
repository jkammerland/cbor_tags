#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_extensions.h"
#include "cbor_tags/cbor_segments.h"

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace cbor::tags::ext::rfc8746 {

template <typename T> struct typed_array_traits;

template <> struct typed_array_traits<std::int32_t> {
    using bit_type                     = std::uint32_t;
    static constexpr std::uint64_t tag = 78;
};

template <> struct typed_array_traits<std::int64_t> {
    using bit_type                     = std::uint64_t;
    static constexpr std::uint64_t tag = 79;
};

template <> struct typed_array_traits<float16_t> {
    using bit_type                     = std::uint16_t;
    static constexpr std::uint64_t tag = 84;
};

template <> struct typed_array_traits<float> {
    using bit_type                     = std::uint32_t;
    static constexpr std::uint64_t tag = 85;
};

template <> struct typed_array_traits<double> {
    using bit_type                     = std::uint64_t;
    static constexpr std::uint64_t tag = 86;
};

template <typename T>
concept IsTypedArrayElement = requires {
    typename typed_array_traits<std::remove_cv_t<T>>::bit_type;
    { typed_array_traits<std::remove_cv_t<T>>::tag } -> std::convertible_to<std::uint64_t>;
};

namespace detail {

template <IsTypedArrayElement T> [[nodiscard]] constexpr auto to_bits(T value) noexcept {
    using value_type = std::remove_cv_t<T>;
    using bit_type   = typename typed_array_traits<value_type>::bit_type;
    static_assert(sizeof(value_type) == sizeof(bit_type));
    return std::bit_cast<bit_type>(value);
}

template <IsTypedArrayElement T> [[nodiscard]] constexpr T from_little_endian(std::span<const std::byte> bytes) noexcept {
    using value_type = std::remove_cv_t<T>;
    using bit_type   = typename typed_array_traits<value_type>::bit_type;
    static_assert(sizeof(value_type) == sizeof(bit_type));

    bit_type bits{};
    for (std::size_t i = 0; i < sizeof(value_type); ++i) {
        bits |= static_cast<bit_type>(std::to_integer<std::uint8_t>(bytes[i])) << (i * 8U);
    }
    return std::bit_cast<value_type>(bits);
}

template <IsTypedArrayElement T> void append_little_endian_bytes(std::vector<std::byte> &out, T value) {
    using value_type = std::remove_cv_t<T>;
    auto bits        = to_bits(static_cast<value_type>(value));
    for (std::size_t i = 0; i < sizeof(value_type); ++i) {
        out.push_back(static_cast<std::byte>((bits >> (i * 8U)) & 0xFFU));
    }
}

template <IsTypedArrayElement T> [[nodiscard]] std::vector<std::byte> little_endian_payload(std::span<const T> values) {
    std::vector<std::byte> bytes;
    bytes.reserve(values.size_bytes());
    for (const auto value : values) {
        append_little_endian_bytes(bytes, value);
    }
    return bytes;
}

template <IsTypedArrayElement T, typename AssignPayload>
[[nodiscard]] status_code decode_payload(auto &dec, major_type major, std::byte additional_info, AssignPayload &&assign_payload) {
    using value_type = std::remove_cv_t<T>;

    if (major != major_type::Tag) {
        return status_code::no_match_for_tag_on_buffer;
    }
    if (dec.decode_unsigned(additional_info) != typed_array_traits<value_type>::tag) {
        return status_code::no_match_for_tag;
    }

    const auto [payload_major, payload_additional_info] = dec.read_initial_byte();
    if (payload_major != major_type::ByteString || payload_additional_info == std::byte{31}) {
        return status_code::no_match_for_bstr_on_buffer;
    }

    return std::forward<AssignPayload>(assign_payload)(payload_major, payload_additional_info);
}

template <IsTypedArrayElement T> [[nodiscard]] std::vector<std::remove_cv_t<T>> copy_values(std::span<const std::byte> payload) {
    using value_type = std::remove_cv_t<T>;

    std::vector<value_type> values;
    values.reserve(payload.size() / sizeof(value_type));
    for (std::size_t offset = 0; offset < payload.size(); offset += sizeof(value_type)) {
        values.push_back(from_little_endian<value_type>(payload.subspan(offset, sizeof(value_type))));
    }
    return values;
}

} // namespace detail

template <IsTypedArrayElement T> class typed_array {
  public:
    using value_type                              = std::remove_cv_t<T>;
    using container_type                          = std::vector<value_type>;
    static constexpr std::uint64_t cbor_array_tag = typed_array_traits<value_type>::tag;

    typed_array() = default;
    explicit typed_array(container_type values) : values_(std::move(values)) {}
    typed_array(std::initializer_list<value_type> values) : values_(values) {}

    [[nodiscard]] container_type             &values() noexcept { return values_; }
    [[nodiscard]] const container_type       &values() const noexcept { return values_; }
    [[nodiscard]] std::span<const value_type> span() const noexcept { return std::span<const value_type>{values_}; }

  private:
    container_type values_{};
};

template <IsTypedArrayElement T> class typed_array_ref {
  public:
    using value_type                              = std::remove_cv_t<T>;
    static constexpr std::uint64_t cbor_array_tag = typed_array_traits<value_type>::tag;

    constexpr explicit typed_array_ref(std::span<const value_type> values) noexcept : values_(values) {}

    [[nodiscard]] constexpr std::span<const value_type> values() const noexcept { return values_; }

  private:
    std::span<const value_type> values_{};
};

template <IsTypedArrayElement T> [[nodiscard]] constexpr auto as_typed_array(std::span<const T> values) noexcept {
    return typed_array_ref<std::remove_cv_t<T>>{std::span<const std::remove_cv_t<T>>{values.data(), values.size()}};
}

template <IsTypedArrayElement T>
    requires(!std::is_const_v<T>)
[[nodiscard]] constexpr auto as_typed_array(std::span<T> values) noexcept {
    return as_typed_array(std::span<const T>{values.data(), values.size()});
}

template <IsTypedArrayElement T, typename Allocator>
[[nodiscard]] constexpr auto as_typed_array(const std::vector<T, Allocator> &values) noexcept {
    return as_typed_array(std::span<const T>{values.data(), values.size()});
}

template <IsTypedArrayElement T, typename Allocator> void as_typed_array(std::vector<T, Allocator> &&values) = delete;

template <IsTypedArrayElement T> class typed_array_view {
  public:
    using value_type                              = std::remove_cv_t<T>;
    static constexpr std::uint64_t cbor_array_tag = typed_array_traits<value_type>::tag;

    constexpr typed_array_view() = default;
    constexpr explicit typed_array_view(std::span<const std::byte> payload) noexcept : payload_(payload) {}

    [[nodiscard]] constexpr std::span<const std::byte> payload_bytes() const noexcept { return payload_; }
    [[nodiscard]] constexpr std::size_t                size() const noexcept { return payload_.size() / sizeof(value_type); }

    [[nodiscard]] auto values() const {
        return std::views::iota(std::size_t{}, size()) | std::views::transform([payload = payload_](std::size_t index) {
                   const auto offset = index * sizeof(value_type);
                   return detail::from_little_endian<value_type>(payload.subspan(offset, sizeof(value_type)));
               });
    }

    [[nodiscard]] std::vector<value_type> copy_values() const { return detail::copy_values<value_type>(payload_); }

  private:
    std::span<const std::byte> payload_{};
};

template <typename Self> struct typed_array_codec : cbor_codec_mixin_base<Self> {
    using cbor_codec_mixin_base<Self>::decode;
    using cbor_codec_mixin_base<Self>::encode;

    template <IsTypedArrayElement T> void encode(const typed_array<T> &array) { encode_values(array.span()); }

    template <IsTypedArrayElement T> void encode(const typed_array_ref<T> &array) { encode_values(array.values()); }

    template <IsTypedArrayElement T> [[nodiscard]] status_code decode(typed_array<T> &array, major_type major, std::byte additional_info) {
        using value_type = std::remove_cv_t<T>;
        auto &dec        = static_cast<Self &>(*this);

        return detail::decode_payload<value_type>(dec, major, additional_info, [&](major_type payload_major, std::byte payload_info) {
            std::vector<std::byte> payload;
            auto                   status = dec.decode(payload, payload_major, payload_info);
            if (status != status_code::success) {
                return status;
            }
            if ((payload.size() % sizeof(value_type)) != 0U) {
                return status_code::unexpected_group_size;
            }
            array.values() = detail::copy_values<value_type>(std::span<const std::byte>{payload});
            return status_code::success;
        });
    }

    template <IsTypedArrayElement T>
    [[nodiscard]] status_code decode(typed_array_view<T> &view, major_type major, std::byte additional_info) {
        using value_type = std::remove_cv_t<T>;
        auto &dec        = static_cast<Self &>(*this);

        return detail::decode_payload<value_type>(dec, major, additional_info, [&](major_type payload_major, std::byte payload_info) {
            if constexpr (!IsContiguous<typename Self::input_buffer_type>) {
                return status_code::contiguous_view_on_non_contiguous_data;
            } else {
                std::basic_string_view<std::byte> payload;
                auto                              status = dec.decode(payload, payload_major, payload_info);
                if (status != status_code::success) {
                    return status;
                }
                if ((payload.size() % sizeof(value_type)) != 0U) {
                    return status_code::unexpected_group_size;
                }
                view = typed_array_view<value_type>{std::span<const std::byte>{payload.data(), payload.size()}};
                return status_code::success;
            }
        });
    }

  private:
    template <IsTypedArrayElement T> void encode_values(std::span<const T> values) {
        using value_type = std::remove_cv_t<T>;
        auto &enc        = static_cast<Self &>(*this);

        if constexpr (std::endian::native == std::endian::little) {
            enc.encode(static_tag<typed_array_traits<value_type>::tag>{});
            enc.encode(std::as_bytes(values));
        } else {
            auto payload = detail::little_endian_payload(values);
            enc.encode(static_tag<typed_array_traits<value_type>::tag>{});
            enc.encode(std::span<const std::byte>{payload});
        }
    }
};

template <IsTypedArrayElement T> [[nodiscard]] cbor_segments encode_typed_array_segments(std::span<const T> values) {
    if constexpr (std::endian::native == std::endian::little) {
        return encode_tagged_bstr_segments(typed_array_traits<std::remove_cv_t<T>>::tag, std::as_bytes(values));
    } else {
        throw std::logic_error("RFC 8746 typed-array segmented zero-copy encode requires a little-endian native payload");
    }
}

template <IsTypedArrayElement T>
    requires(!std::is_const_v<T>)
[[nodiscard]] cbor_segments encode_typed_array_segments(std::span<T> values) {
    return encode_typed_array_segments(std::span<const T>{values.data(), values.size()});
}

template <IsTypedArrayElement T> [[nodiscard]] cbor_segments encode_typed_array_segments_copy(std::span<const T> values) {
    const auto tag_header =
        cbor::tags::detail::encode_cbor_major_argument_header(typed_array_traits<std::remove_cv_t<T>>::tag, std::byte{0xC0});
    auto       payload     = detail::little_endian_payload(values);
    const auto bstr_header = cbor::tags::detail::encode_cbor_major_argument_header(payload.size(), std::byte{0x40});

    cbor_segments segments;
    segments.reserve(3);
    segments.append_owned(tag_header);
    segments.append_owned(bstr_header);
    segments.append_owned(payload);
    return segments;
}

template <IsTypedArrayElement T>
    requires(!std::is_const_v<T>)
[[nodiscard]] cbor_segments encode_typed_array_segments_copy(std::span<T> values) {
    return encode_typed_array_segments_copy(std::span<const T>{values.data(), values.size()});
}

template <IsTypedArrayElement T> [[nodiscard]] cbor_segments encode_segments(const typed_array_ref<T> &array) {
    return encode_typed_array_segments(array.values());
}

template <IsTypedArrayElement T> [[nodiscard]] cbor_segments encode_segments(const typed_array<T> &array) {
    return encode_typed_array_segments(array.span());
}

} // namespace cbor::tags::ext::rfc8746
