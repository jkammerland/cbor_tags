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

template <typename T> struct custom_codec_1_ref {
    T *value_{};
};

template <typename T> struct custom_codec_1_payload_ref {
    T *value_{};
};

template <typename Tag, typename T> struct custom_codec_1_tag_ref {
    Tag tag_{};
    T  *value_{};
};

template <typename T> constexpr custom_codec_1_ref<T> as_custom_codec_1(T &value) noexcept { return {std::addressof(value)}; }

template <typename T> constexpr custom_codec_1_ref<const T> as_custom_codec_1(const T &value) noexcept { return {std::addressof(value)}; }

template <typename T>
    requires(!std::is_lvalue_reference_v<T>)
void as_custom_codec_1(T &&) = delete;

template <typename T>
    requires(!std::is_const_v<T>)
constexpr custom_codec_1_payload_ref<T> as_custom_codec_1_payload(T &value) noexcept {
    return {std::addressof(value)};
}

template <typename T>
    requires(std::is_const_v<std::remove_reference_t<T>> || !std::is_lvalue_reference_v<T>)
void as_custom_codec_1_payload(T &&) = delete;

template <typename Tag, typename T>
constexpr custom_codec_1_tag_ref<std::remove_cvref_t<Tag>, T> as_custom_codec_1(Tag &&tag, T &value) noexcept {
    return {std::forward<Tag>(tag), std::addressof(value)};
}

template <typename Tag, typename T>
constexpr custom_codec_1_tag_ref<std::remove_cvref_t<Tag>, const T> as_custom_codec_1(Tag &&tag, const T &value) noexcept {
    return {std::forward<Tag>(tag), std::addressof(value)};
}

template <typename Tag, typename T>
    requires(!std::is_lvalue_reference_v<T>)
void as_custom_codec_1(Tag &&, T &&) = delete;

template <typename Self> struct custom_codec_1 : cbor::tags::cbor_codec_mixin_base<Self> {
    using cbor::tags::cbor_codec_mixin_base<Self>::decode;
    using cbor::tags::cbor_codec_mixin_base<Self>::encode;

    template <typename T> constexpr void encode(const custom_codec_1_ref<T> &value) {
        encode_tagged(cbor::tags::detail::custom_codec_1::tag_for(*value.value_), *value.value_);
    }

    template <typename Tag, typename T> constexpr void encode(const custom_codec_1_tag_ref<Tag, T> &value) {
        encode_tagged(cbor::tags::detail::custom_codec_1::tag_to_uint64(value.tag_), *value.value_);
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
        return decode_tagged(cbor::tags::detail::custom_codec_1::tag_for(*value.value_), *value.value_, major, additional_info);
    }

    template <typename T>
    [[nodiscard]] constexpr status_code decode(custom_codec_1_payload_ref<T> value, major_type major, std::byte additional_info) {
        return decode_payload_bstr(*value.value_, major, additional_info);
    }

    template <typename Tag, typename T>
    [[nodiscard]] constexpr status_code decode(custom_codec_1_tag_ref<Tag, T> value, major_type major, std::byte additional_info) {
        return decode_tagged(cbor::tags::detail::custom_codec_1::tag_to_uint64(value.tag_), *value.value_, major, additional_info);
    }

  private:
    template <typename T> constexpr void encode_tagged(std::uint64_t tag, const T &value) {
        auto &enc          = static_cast<Self &>(*this);
        auto  payload_size = cbor::tags::detail::custom_codec_1::encoded_size(value);

        enc.encode_major_and_size(tag, static_cast<typename Self::byte_type>(0xC0));
        enc.encode_major_and_size(static_cast<std::uint64_t>(payload_size), static_cast<typename Self::byte_type>(0x40));
        cbor::tags::detail::custom_codec_1::encode_payload_to(enc.appender_, enc.data_, value);
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
