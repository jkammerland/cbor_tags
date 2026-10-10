#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/codec.h"
#include "cbor_tags/detail/cbor_argument.h"
#include "cbor_tags/detail/cbor_extension_decode.h"
#include "cbor_tags/detail/cbor_extension_encode.h"
#include "cbor_tags/detail/custom_codec_1_serialization.h"

#include <cstddef>
#include <memory>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace cbor::tags::custom_1 {

namespace cbor_detail  = tags::detail;
namespace codec_detail = tags::detail::custom_codec_1;

template <typename T> class ref {
  public:
    constexpr explicit ref(T &value) noexcept : value_(std::addressof(value)) {}

    [[nodiscard]] constexpr T &get() const noexcept { return *value_; }

  private:
    T *value_;
};

template <typename T> class payload_ref {
  public:
    constexpr explicit payload_ref(T &value) noexcept : value_(std::addressof(value)) {}

    [[nodiscard]] constexpr T &get() const noexcept { return *value_; }

  private:
    T *value_;
};

template <typename Tag, typename T> class tag_ref {
  public:
    constexpr tag_ref(Tag tag, T &value) noexcept : tag_(std::move(tag)), value_(std::addressof(value)) {}

    [[nodiscard]] constexpr const Tag &tag() const noexcept { return tag_; }
    [[nodiscard]] constexpr T         &get() const noexcept { return *value_; }

  private:
    Tag tag_;
    T  *value_;
};

template <typename T> constexpr ref<T> as_ref(T &value) noexcept { return ref<T>{value}; }

template <typename T> constexpr ref<const T> as_ref(const T &value) noexcept { return ref<const T>{value}; }

template <typename T>
    requires(!std::is_lvalue_reference_v<T>)
void as_ref(T &&) = delete;

template <typename T>
    requires(!std::is_const_v<T>)
constexpr payload_ref<T> as_payload(T &value) noexcept {
    return payload_ref<T>{value};
}

template <typename T>
    requires(std::is_const_v<std::remove_reference_t<T>> || !std::is_lvalue_reference_v<T>)
void as_payload(T &&) = delete;

template <typename Tag, typename T> constexpr tag_ref<std::remove_cvref_t<Tag>, T> as_ref(Tag &&tag, T &value) noexcept {
    return tag_ref<std::remove_cvref_t<Tag>, T>{std::forward<Tag>(tag), value};
}

template <typename Tag, typename T> constexpr tag_ref<std::remove_cvref_t<Tag>, const T> as_ref(Tag &&tag, const T &value) noexcept {
    return tag_ref<std::remove_cvref_t<Tag>, const T>{std::forward<Tag>(tag), value};
}

template <typename Tag, typename T>
    requires(!std::is_lvalue_reference_v<T>)
void as_ref(Tag &&, T &&) = delete;

template <typename Tag, typename T> [[nodiscard]] inline cbor_segments encode_borrowed_segments(Tag &&tag, const T &value) {
    auto payload = codec_detail::encode_payload_borrowed_segments(value);

    cbor_segments segments;
    segments.reserve_segments(payload.size() + 2U);
    segments.append_owned(cbor_detail::encode_cbor_tag_header(codec_detail::tag_to_uint64(tag)).span());
    segments.append_owned(cbor_detail::encode_cbor_bstr_header(payload.total_size()).span());

    for (const auto &segment : payload) {
        const auto bytes = segment.bytes();
        if (segment.is_borrowed()) {
            segments.append_borrowed(bytes);
        } else {
            segments.append_owned(bytes);
        }
    }
    return segments;
}

template <typename T> [[nodiscard]] inline cbor_segments encode_borrowed_segments(const T &value) {
    return encode_borrowed_segments(codec_detail::tag_for(value), value);
}

template <typename T>
    requires(!std::is_lvalue_reference_v<T>)
void encode_borrowed_segments(T &&) = delete;

template <typename Tag, typename T>
    requires(!std::is_lvalue_reference_v<T>)
void encode_borrowed_segments(Tag &&, T &&) = delete;

} // namespace cbor::tags::custom_1

namespace cbor::tags::codec {

template <typename Self> struct custom_1 : base<Self> {
    using base<Self>::decode;
    using base<Self>::encode;

    template <typename T> constexpr void encode(const tags::custom_1::ref<T> &value) {
        encode_tagged(tags::detail::custom_codec_1::tag_for(value.get()), value.get());
    }

    template <typename Tag, typename T> constexpr void encode(const tags::custom_1::tag_ref<Tag, T> &value) {
        encode_tagged(tags::detail::custom_codec_1::tag_to_uint64(value.tag()), value.get());
    }

    template <typename T> [[nodiscard]] constexpr status_code decode(tags::custom_1::ref<T> value) {
        auto      &dec = static_cast<Self &>(*this);
        major_type major{};
        std::byte  additional_info{};
        auto       status = tags::detail::read_initial_byte(dec, major, additional_info);
        if (status != status_code::success) {
            return status;
        }
        return decode(value, major, additional_info);
    }

    template <typename T> [[nodiscard]] constexpr status_code decode(tags::custom_1::payload_ref<T> value) {
        auto      &dec = static_cast<Self &>(*this);
        major_type major{};
        std::byte  additional_info{};
        auto       status = tags::detail::read_initial_byte(dec, major, additional_info);
        if (status != status_code::success) {
            return status;
        }
        return decode(value, major, additional_info);
    }

    template <typename Tag, typename T> [[nodiscard]] constexpr status_code decode(tags::custom_1::tag_ref<Tag, T> value) {
        auto      &dec = static_cast<Self &>(*this);
        major_type major{};
        std::byte  additional_info{};
        auto       status = tags::detail::read_initial_byte(dec, major, additional_info);
        if (status != status_code::success) {
            return status;
        }
        return decode(value, major, additional_info);
    }

    template <typename T>
    [[nodiscard]] constexpr status_code decode(tags::custom_1::ref<T> value, major_type major, std::byte additional_info) {
        return decode_tagged(tags::detail::custom_codec_1::tag_for(value.get()), value.get(), major, additional_info);
    }

    template <typename T>
    [[nodiscard]] constexpr status_code decode(tags::custom_1::payload_ref<T> value, major_type major, std::byte additional_info) {
        return decode_payload_bstr(value.get(), major, additional_info);
    }

    template <typename Tag, typename T>
    [[nodiscard]] constexpr status_code decode(tags::custom_1::tag_ref<Tag, T> value, major_type major, std::byte additional_info) {
        return decode_tagged(tags::detail::custom_codec_1::tag_to_uint64(value.tag()), value.get(), major, additional_info);
    }

  private:
    template <typename T> constexpr void encode_tagged(std::uint64_t tag, const T &value) {
        auto &enc     = static_cast<Self &>(*this);
        auto  payload = tags::detail::make_extension_payload_for_output(
            enc, [&value](auto &appender, auto &output) { tags::detail::custom_codec_1::encode_payload_to(appender, output, value); },
            [&value] { return tags::detail::custom_codec_1::encode_payload_segments(value); });

        tags::detail::encode_extension_tagged_bstr_header(enc, tag,
                                                          static_cast<std::uint64_t>(tags::detail::extension_payload_size(payload)));
        tags::detail::append_extension_payload(enc, payload);
    }

    template <typename T>
    [[nodiscard]] constexpr status_code decode_tagged(std::uint64_t expected_tag, T &value, major_type major, std::byte additional_info) {
        auto &dec = static_cast<Self &>(*this);
        return tags::detail::decode_tagged_bstr_payload_header(dec, expected_tag, major, additional_info, [&](std::byte payload_info) {
            return decode_payload_bstr(value, major_type::ByteString, payload_info);
        });
    }

    template <typename T>
    [[nodiscard]] constexpr status_code decode_payload_bstr(T &value, major_type payload_major, std::byte payload_info) {
        auto &dec = static_cast<Self &>(*this);
        if (payload_major != major_type::ByteString || payload_info == std::byte{31}) {
            return status_code::no_match_for_bstr_on_buffer;
        }

        std::uint64_t payload_size{};
        auto          status = tags::detail::decode_unsigned_argument(dec, payload_info, payload_size);
        if (status != status_code::success) {
            return status;
        }
        using payload_type = decltype(std::declval<Self &>().decode_bstring_payload(std::declval<std::uint64_t>()));
        if constexpr (!std::ranges::contiguous_range<payload_type> && tags::detail::custom_codec_1::has_borrowed_decode_refs_v<T>) {
            return status_code::contiguous_view_on_non_contiguous_data;
        }

        if constexpr (std::ranges::contiguous_range<payload_type>) {
            return tags::detail::consume_extension_bstring_payload(dec, payload_size, [&](auto &&payload) {
                return tags::detail::custom_codec_1::decode_payload(
                    std::span<const std::byte>(std::ranges::data(payload), std::ranges::size(payload)), value);
            });
        } else {
            std::vector<std::byte> payload_bytes;
            status = tags::detail::decode_extension_bstring_payload_into(dec, payload_size, payload_bytes);
            if (status != status_code::success) {
                return status;
            }
            return tags::detail::custom_codec_1::decode_payload(std::span<const std::byte>(payload_bytes.data(), payload_bytes.size()),
                                                                value);
        }
    }
};

} // namespace cbor::tags::codec
