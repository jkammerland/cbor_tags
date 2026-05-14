#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_extensions.h"

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

#if !defined(__has_include)
#error "cbor_tags std::expected support requires __has_include and C++23 <expected>"
#endif

#if !__has_include(<expected>)
#error "cbor_tags std::expected support requires C++23 <expected>"
#endif

#if __has_include(<version>)
#include <version>
#endif

#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202202L
#error "cbor_tags std::expected support requires C++23 std::expected"
#endif

#include <expected>

namespace cbor::tags::ext::std_expected {

namespace detail {

template <typename T, typename E>
concept DecodableStdExpected = std::default_initializable<E> && (std::is_void_v<T> || std::default_initializable<T>);

} // namespace detail

template <typename Self> struct std_expected_codec : cbor_codec_mixin_base<Self> {
    using cbor_codec_mixin_base<Self>::decode;
    using cbor_codec_mixin_base<Self>::encode;

    template <typename T, typename E> constexpr void encode(const std::expected<T, E> &value) {
        auto &enc = static_cast<Self &>(*this);

        enc.encode(as_array{2});
        enc.encode(value.has_value());
        if (value.has_value()) {
            if constexpr (std::is_void_v<T>) {
                enc.encode(nullptr);
            } else {
                enc.encode(*value);
            }
        } else {
            enc.encode(value.error());
        }
    }

    template <typename T, typename E>
        requires detail::DecodableStdExpected<T, E>
    [[nodiscard]] constexpr status_code decode(std::expected<T, E> &value, major_type major, std::byte additional_info) {
        auto &dec = static_cast<Self &>(*this);

        if (major != major_type::Array) {
            return status_code::no_match_for_array_on_buffer;
        }

        if (additional_info == static_cast<std::byte>(31)) {
            auto status = decode_payload(value);
            if (status != status_code::success) {
                return status;
            }

            const auto [end_major, end_info] = dec.read_initial_byte();
            if (end_major != major_type::Simple || end_info != static_cast<std::byte>(31)) {
                return status_code::unexpected_group_size;
            }
            return status_code::success;
        }

        const auto size = dec.decode_unsigned(additional_info);
        if (size != 2U) {
            return status_code::unexpected_group_size;
        }

        return decode_payload(value);
    }

  private:
    template <typename T, typename E>
        requires detail::DecodableStdExpected<T, E>
    [[nodiscard]] constexpr status_code decode_payload(std::expected<T, E> &value) {
        auto &dec = static_cast<Self &>(*this);

        bool has_value{};
        auto status = dec.decode(has_value);
        if (status != status_code::success) {
            return status;
        }

        if (has_value) {
            if constexpr (std::is_void_v<T>) {
                std::nullptr_t decoded_value{};
                status = dec.decode(decoded_value);
                if (status == status_code::success) {
                    value = std::expected<void, E>{};
                }
            } else {
                T decoded_value{};
                status = dec.decode(decoded_value);
                if (status == status_code::success) {
                    value = std::expected<T, E>{std::move(decoded_value)};
                }
            }
        } else {
            E decoded_error{};
            status = dec.decode(decoded_error);
            if (status == status_code::success) {
                value = std::unexpected<E>{std::move(decoded_error)};
            }
        }

        return status;
    }
};

} // namespace cbor::tags::ext::std_expected
