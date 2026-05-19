#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_extensions.h"
#include "cbor_tags/detail/cbor_argument.h"
#include "cbor_tags/detail/custom_codec_1_serialization.h"

#include <cstddef>
#include <limits>
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
        auto &dec = static_cast<Self &>(*this);
        if (dec.reader_.empty(dec.data_)) {
            return status_code::incomplete;
        }
        auto [major, additional_info] = dec.read_initial_byte();
        return decode(value, major, additional_info);
    }

    template <typename T> [[nodiscard]] constexpr status_code decode(custom_codec_1_payload_ref<T> value) {
        auto &dec = static_cast<Self &>(*this);
        if (dec.reader_.empty(dec.data_)) {
            return status_code::incomplete;
        }
        auto [major, additional_info] = dec.read_initial_byte();
        return decode(value, major, additional_info);
    }

    template <typename Tag, typename T> [[nodiscard]] constexpr status_code decode(custom_codec_1_tag_ref<Tag, T> value) {
        auto &dec = static_cast<Self &>(*this);
        if (dec.reader_.empty(dec.data_)) {
            return status_code::incomplete;
        }
        auto [major, additional_info] = dec.read_initial_byte();
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

        enc.encode_major_and_size(tag, static_cast<typename Self::byte_type>(0xC0));
        enc.encode_major_and_size(static_cast<std::uint64_t>(payload_size(payload)), static_cast<typename Self::byte_type>(0x40));
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
            cbor::tags::detail::append_byte_range(enc.appender_, enc.data_, payload);
        }
    }

    template <typename Segments> static constexpr void append_payload_segments(Self &enc, const Segments &payload) {
        for (const auto &segment : payload) {
            append_payload_segment(enc, segment);
        }
    }

    template <typename Segment> static constexpr void append_payload_segment(Self &enc, const Segment &segment) {
        const auto bytes = segment.bytes();
        if constexpr (requires {
                          enc.data_.append_borrowed(bytes);
                          cbor::tags::detail::append_owned_segment(enc.data_, bytes);
                      }) {
            if (segment.is_borrowed()) {
                enc.data_.append_borrowed(bytes);
            } else {
                cbor::tags::detail::append_owned_segment(enc.data_, bytes);
            }
        } else {
            cbor::tags::detail::append_segment_to_encoder(enc, segment);
        }
    }

    template <typename T>
    [[nodiscard]] constexpr status_code decode_tagged(std::uint64_t expected_tag, T &value, major_type major, std::byte additional_info) {
        auto &dec = static_cast<Self &>(*this);
        if (major != major_type::Tag) {
            return status_code::no_match_for_tag_on_buffer;
        }

        auto argument_status = validate_argument_available(dec, additional_info);
        if (argument_status != status_code::success) {
            return argument_status;
        }
        const auto actual_tag = dec.decode_unsigned(additional_info);
        if (actual_tag != expected_tag) {
            return status_code::no_match_for_tag;
        }

        if (dec.reader_.empty(dec.data_)) {
            return status_code::incomplete;
        }

        auto [payload_major, payload_info] = dec.read_initial_byte();
        return decode_payload_bstr(value, payload_major, payload_info);
    }

    template <typename T>
    [[nodiscard]] constexpr status_code decode_payload_bstr(T &value, major_type payload_major, std::byte payload_info) {
        auto &dec = static_cast<Self &>(*this);
        if (payload_major != major_type::ByteString || payload_info == std::byte{31}) {
            return status_code::no_match_for_bstr_on_buffer;
        }

        auto argument_status = validate_argument_available(dec, payload_info);
        if (argument_status != status_code::success) {
            return argument_status;
        }
        const auto payload_size = dec.decode_unsigned(payload_info);
        if constexpr (std::numeric_limits<typename Self::size_type>::max() < std::numeric_limits<std::uint64_t>::max()) {
            if (payload_size > static_cast<std::uint64_t>(std::numeric_limits<typename Self::size_type>::max())) {
                return status_code::error;
            }
        }
        if (payload_size > 0U && dec.reader_.empty(dec.data_, static_cast<typename Self::size_type>(payload_size - 1U))) {
            return status_code::incomplete;
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

    [[nodiscard]] static constexpr status_code validate_argument_available(Self &dec, std::byte additional_info) {
        const auto info = std::to_integer<std::uint8_t>(additional_info);
        if (!cbor::tags::detail::is_valid_cbor_argument_info(info)) {
            return status_code::error;
        }

        const auto payload_size = cbor::tags::detail::cbor_argument_payload_size(info);
        if (payload_size > 0U && dec.reader_.empty(dec.data_, static_cast<typename Self::size_type>(payload_size - 1U))) {
            return status_code::incomplete;
        }
        return status_code::success;
    }
};

} // namespace cbor::tags::ext::custom_codec_1
