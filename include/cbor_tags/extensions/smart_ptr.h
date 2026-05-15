#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_extensions.h"

#include <concepts>
#include <cstddef>
#include <memory>
#include <type_traits>

namespace cbor::tags::ext::smart_ptr {

namespace detail {

constexpr bool is_null(major_type major, std::byte additional_info) {
    return major == major_type::Simple && additional_info == static_cast<std::byte>(SimpleType::Null);
}

template <typename T>
concept NullablePointerValue = !std::is_void_v<T> && !std::is_array_v<T> && !std::is_const_v<T>;

template <typename Decoder, typename T> [[nodiscard]] status_code decode_from_consumed_initial_byte(Decoder &dec, T &value) {
    dec.reader_.seek(-1);
    return dec.decode(value);
}

} // namespace detail

template <typename Self> struct nullable_ptr_codec : cbor_codec_mixin_base<Self> {
    using cbor_codec_mixin_base<Self>::decode;
    using cbor_codec_mixin_base<Self>::encode;

    template <detail::NullablePointerValue T> void encode(const std::unique_ptr<T> &value) {
        auto &enc = static_cast<Self &>(*this);
        if (!value) {
            enc.encode(nullptr);
            return;
        }

        enc.encode(*value);
    }

    template <detail::NullablePointerValue T> void encode(const std::shared_ptr<T> &value) {
        auto &enc = static_cast<Self &>(*this);
        if (!value) {
            enc.encode(nullptr);
            return;
        }

        enc.encode(*value);
    }

    template <detail::NullablePointerValue T>
        requires std::default_initializable<T>
    [[nodiscard]] status_code decode(std::unique_ptr<T> &value, major_type major, std::byte additional_info) {
        if (detail::is_null(major, additional_info)) {
            value.reset();
            return status_code::success;
        }

        auto decoded = std::make_unique<T>();
        auto status  = detail::decode_from_consumed_initial_byte(static_cast<Self &>(*this), *decoded);
        if (status == status_code::success) {
            value = std::move(decoded);
        }
        return status;
    }

    template <detail::NullablePointerValue T>
        requires std::default_initializable<T>
    [[nodiscard]] status_code decode(std::shared_ptr<T> &value, major_type major, std::byte additional_info) {
        if (detail::is_null(major, additional_info)) {
            value.reset();
            return status_code::success;
        }

        auto decoded = std::make_shared<T>();
        auto status  = detail::decode_from_consumed_initial_byte(static_cast<Self &>(*this), *decoded);
        if (status == status_code::success) {
            value = std::move(decoded);
        }
        return status;
    }
};

} // namespace cbor::tags::ext::smart_ptr
