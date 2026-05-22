#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_extensions.h"
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

namespace cbor::tags::ext::custom_codec_1 {

template <typename T> class custom_codec_1_ref {
  public:
    constexpr explicit custom_codec_1_ref(T &value) noexcept : value_(std::addressof(value)) {}

    [[nodiscard]] constexpr T &get() const noexcept { return *value_; }

  private:
    T *value_;
};

template <typename T> class custom_codec_1_payload_ref {
  public:
    constexpr explicit custom_codec_1_payload_ref(T &value) noexcept : value_(std::addressof(value)) {}

    [[nodiscard]] constexpr T &get() const noexcept { return *value_; }

  private:
    T *value_;
};

template <typename Tag, typename T> class custom_codec_1_tag_ref {
  public:
    constexpr custom_codec_1_tag_ref(Tag tag, T &value) noexcept : tag_(std::move(tag)), value_(std::addressof(value)) {}

    [[nodiscard]] constexpr const Tag &tag() const noexcept { return tag_; }
    [[nodiscard]] constexpr T         &get() const noexcept { return *value_; }

  private:
    Tag tag_;
    T  *value_;
};

template <typename T> constexpr custom_codec_1_ref<T> as_custom_codec_1(T &value) noexcept { return custom_codec_1_ref<T>{value}; }

template <typename T> constexpr custom_codec_1_ref<const T> as_custom_codec_1(const T &value) noexcept {
    return custom_codec_1_ref<const T>{value};
}

template <typename T>
    requires(!std::is_lvalue_reference_v<T>)
void as_custom_codec_1(T &&) = delete;

template <typename T>
    requires(!std::is_const_v<T>)
constexpr custom_codec_1_payload_ref<T> as_custom_codec_1_payload(T &value) noexcept {
    return custom_codec_1_payload_ref<T>{value};
}

template <typename T>
    requires(std::is_const_v<std::remove_reference_t<T>> || !std::is_lvalue_reference_v<T>)
void as_custom_codec_1_payload(T &&) = delete;

template <typename Tag, typename T>
constexpr custom_codec_1_tag_ref<std::remove_cvref_t<Tag>, T> as_custom_codec_1(Tag &&tag, T &value) noexcept {
    return custom_codec_1_tag_ref<std::remove_cvref_t<Tag>, T>{std::forward<Tag>(tag), value};
}

template <typename Tag, typename T>
constexpr custom_codec_1_tag_ref<std::remove_cvref_t<Tag>, const T> as_custom_codec_1(Tag &&tag, const T &value) noexcept {
    return custom_codec_1_tag_ref<std::remove_cvref_t<Tag>, const T>{std::forward<Tag>(tag), value};
}

template <typename Tag, typename T>
    requires(!std::is_lvalue_reference_v<T>)
void as_custom_codec_1(Tag &&, T &&) = delete;

template <typename Tag, typename T> [[nodiscard]] inline cbor_segments encode_borrowed_segments(Tag &&tag, const T &value) {
    auto payload = cbor::tags::detail::custom_codec_1::encode_payload_borrowed_segments(value);

    cbor_segments segments;
    segments.reserve_segments(payload.size() + 2U);
    segments.append_owned(
        cbor::tags::detail::encode_cbor_major_argument_header(cbor::tags::detail::custom_codec_1::tag_to_uint64(tag), std::byte{0xC0})
            .span());
    segments.append_owned(cbor::tags::detail::encode_cbor_major_argument_header(payload.total_size(), std::byte{0x40}).span());

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
    return encode_borrowed_segments(cbor::tags::detail::custom_codec_1::tag_for(value), value);
}

template <typename T>
    requires(!std::is_lvalue_reference_v<T>)
void encode_borrowed_segments(T &&) = delete;

template <typename Tag, typename T>
    requires(!std::is_lvalue_reference_v<T>)
void encode_borrowed_segments(Tag &&, T &&) = delete;

template <typename Self> struct custom_codec_1 : cbor::tags::cbor_codec_mixin_base<Self> {
    using cbor::tags::cbor_codec_mixin_base<Self>::decode;
    using cbor::tags::cbor_codec_mixin_base<Self>::encode;

    template <typename T> constexpr void encode(const custom_codec_1_ref<T> &value) {
        encode_tagged(cbor::tags::detail::custom_codec_1::tag_for(value.get()), value.get());
    }

    template <typename Tag, typename T> constexpr void encode(const custom_codec_1_tag_ref<Tag, T> &value) {
        encode_tagged(cbor::tags::detail::custom_codec_1::tag_to_uint64(value.tag()), value.get());
    }

    template <typename T> [[nodiscard]] constexpr status_code decode(custom_codec_1_ref<T> value) {
        auto      &dec = static_cast<Self &>(*this);
        major_type major{};
        std::byte  additional_info{};
        auto       status = cbor::tags::detail::read_extension_initial_byte(dec, major, additional_info);
        if (status != status_code::success) {
            return status;
        }
        return decode(value, major, additional_info);
    }

    template <typename T> [[nodiscard]] constexpr status_code decode(custom_codec_1_payload_ref<T> value) {
        auto      &dec = static_cast<Self &>(*this);
        major_type major{};
        std::byte  additional_info{};
        auto       status = cbor::tags::detail::read_extension_initial_byte(dec, major, additional_info);
        if (status != status_code::success) {
            return status;
        }
        return decode(value, major, additional_info);
    }

    template <typename Tag, typename T> [[nodiscard]] constexpr status_code decode(custom_codec_1_tag_ref<Tag, T> value) {
        auto      &dec = static_cast<Self &>(*this);
        major_type major{};
        std::byte  additional_info{};
        auto       status = cbor::tags::detail::read_extension_initial_byte(dec, major, additional_info);
        if (status != status_code::success) {
            return status;
        }
        return decode(value, major, additional_info);
    }

    template <typename T>
    [[nodiscard]] constexpr status_code decode(custom_codec_1_ref<T> value, major_type major, std::byte additional_info) {
        return decode_tagged(cbor::tags::detail::custom_codec_1::tag_for(value.get()), value.get(), major, additional_info);
    }

    template <typename T>
    [[nodiscard]] constexpr status_code decode(custom_codec_1_payload_ref<T> value, major_type major, std::byte additional_info) {
        return decode_payload_bstr(value.get(), major, additional_info);
    }

    template <typename Tag, typename T>
    [[nodiscard]] constexpr status_code decode(custom_codec_1_tag_ref<Tag, T> value, major_type major, std::byte additional_info) {
        return decode_tagged(cbor::tags::detail::custom_codec_1::tag_to_uint64(value.tag()), value.get(), major, additional_info);
    }

  private:
    template <typename T> constexpr void encode_tagged(std::uint64_t tag, const T &value) {
        auto &enc     = static_cast<Self &>(*this);
        auto  payload = encode_payload_for_output(enc.data_, value);

        cbor::tags::detail::encode_extension_tag_header(enc, tag);
        cbor::tags::detail::encode_extension_bstr_header(enc, static_cast<std::uint64_t>(payload_size(payload)));
        append_payload(enc, payload);
    }

    template <typename Output, typename T> [[nodiscard]] static auto encode_payload_for_output(Output &output, const T &value) {
        using output_type = std::remove_cvref_t<Output>;
        if constexpr (CborAppendOutputBuffer<output_type> && requires { output_type{output.get_allocator()}; }) {
            output_type                               payload{output.get_allocator()};
            cbor::tags::detail::appender<output_type> appender;
            cbor::tags::detail::custom_codec_1::encode_payload_to(appender, payload, value);
            return payload;
        } else {
            return cbor::tags::detail::custom_codec_1::encode_payload_segments(value);
        }
    }

    template <typename Payload> [[nodiscard]] static constexpr std::size_t payload_size(const Payload &payload) noexcept {
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

    template <typename Payload> static constexpr void append_payload(Self &enc, const Payload &payload) {
        if constexpr (CborSegmentOutputBuffer<std::remove_cvref_t<Payload>>) {
            append_payload_segments(enc, payload);
        } else {
            cbor::tags::detail::append_extension_owned_bytes(enc, payload);
        }
    }

    template <typename Segments> static constexpr void append_payload_segments(Self &enc, const Segments &payload) {
        for (const auto &segment : payload) {
            append_payload_segment(enc, segment);
        }
    }

    template <typename Segment> static constexpr void append_payload_segment(Self &enc, const Segment &segment) {
        cbor::tags::detail::append_extension_segment(enc, segment);
    }

    template <typename T>
    [[nodiscard]] constexpr status_code decode_tagged(std::uint64_t expected_tag, T &value, major_type major, std::byte additional_info) {
        auto &dec = static_cast<Self &>(*this);
        if (major != major_type::Tag) {
            return status_code::no_match_for_tag_on_buffer;
        }

        std::uint64_t actual_tag{};
        auto          status = cbor::tags::detail::decode_unsigned_argument(dec, additional_info, actual_tag);
        if (status != status_code::success) {
            return status;
        }
        if (actual_tag != expected_tag) {
            return status_code::no_match_for_tag;
        }

        major_type payload_major{};
        std::byte  payload_info{};
        status = cbor::tags::detail::read_extension_initial_byte(dec, payload_major, payload_info);
        if (status != status_code::success) {
            return status;
        }

        return decode_payload_bstr(value, payload_major, payload_info);
    }

    template <typename T>
    [[nodiscard]] constexpr status_code decode_payload_bstr(T &value, major_type payload_major, std::byte payload_info) {
        auto &dec = static_cast<Self &>(*this);
        if (payload_major != major_type::ByteString || payload_info == std::byte{31}) {
            return status_code::no_match_for_bstr_on_buffer;
        }

        std::uint64_t payload_size{};
        auto          status = cbor::tags::detail::decode_unsigned_argument(dec, payload_info, payload_size);
        if (status != status_code::success) {
            return status;
        }
        status = cbor::tags::detail::require_extension_payload_bytes(dec, payload_size);
        if (status != status_code::success) {
            return status;
        }

        using payload_type = decltype(std::declval<Self &>().decode_bstring_payload(std::declval<std::uint64_t>()));
        if constexpr (!std::ranges::contiguous_range<payload_type> && cbor::tags::detail::custom_codec_1::has_borrowed_decode_refs_v<T>) {
            return status_code::contiguous_view_on_non_contiguous_data;
        }

        auto payload = dec.decode_bstring_payload(payload_size);
        if constexpr (std::ranges::contiguous_range<decltype(payload)>) {
            return cbor::tags::detail::custom_codec_1::decode_payload(
                std::span<const std::byte>(std::ranges::data(payload), std::ranges::size(payload)), value);
        } else {
            std::vector<std::byte> payload_bytes(payload.begin(), payload.end());
            return cbor::tags::detail::custom_codec_1::decode_payload(
                std::span<const std::byte>(payload_bytes.data(), payload_bytes.size()), value);
        }
    }
};

} // namespace cbor::tags::ext::custom_codec_1
