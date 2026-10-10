#pragma once

#include "cbor_tags/codec.h"
#include "cbor_tags/detail/cbor_encode_error.h"
#include "cbor_tags/detail/cbor_item_shape.h"
#include "cbor_tags/detail/cbor_optional_variant_traits.h"
#include "cbor_tags/extensions/cddl_traits.h"

#include <memory>
#include <type_traits>
#include <utility>
#include <version>

#if !defined(__cpp_lib_indirect) || __cpp_lib_indirect < 202502L
#error "cbor_tags std::indirect support requires C++26 std::indirect"
#endif

namespace cbor::tags::codec {

namespace detail {

template <typename T> struct is_indirect : std::false_type {};
template <typename T, typename Alloc> struct is_indirect<std::indirect<T, Alloc>> : std::true_type {};

template <typename T> consteval bool has_indirect_alternative() {
    return tags::detail::has_matching_alternative<T, []<typename U>() { return is_indirect<U>::value; }>();
}

} // namespace detail

template <typename Self> struct std_indirect : base<Self> {
    using base<Self>::decode;
    using base<Self>::encode;

    template <typename T, typename Alloc> constexpr void encode(const std::indirect<T, Alloc> &value) {
        require_single_item<T>();
        if (value.valueless_after_move()) {
            throw tags::detail::encode_status_exception{status_code::error};
        }
        static_cast<Self &>(*this).encode(*value);
    }

    template <typename T, typename Alloc>
        requires std::default_initializable<T>
    [[nodiscard]] constexpr status_code decode(std::indirect<T, Alloc> &value, major_type major, std::byte additional_info) {
        require_single_item<T>();
        if (value.valueless_after_move()) {
            std::indirect<T, Alloc> replacement(std::allocator_arg, value.get_allocator());
            // Equal allocators let swap transfer ownership without moving T.
            value.swap(replacement);
        }
        return static_cast<Self &>(*this).decode(*value, major, additional_info);
    }

    template <IsVariant Variant>
        requires(detail::has_indirect_alternative<Variant>())
    void encode(const Variant &) {
        static_assert(always_false<Variant>::value,
                      "std::indirect variant alternatives require an explicit application wire variant; put the variant inside indirect");
    }

    template <IsVariant Variant>
        requires(detail::has_indirect_alternative<Variant>())
    [[nodiscard]] status_code decode(Variant &, major_type, std::byte) {
        static_assert(always_false<Variant>::value,
                      "std::indirect variant alternatives require an explicit application wire variant; put the variant inside indirect");
        return status_code::error;
    }

  private:
    template <typename T> static consteval void require_single_item() {
        static_assert(tags::detail::codec_payload_encodes_one_item<Self, T>(
                          tags::detail::payload_context<tags::detail::payload_decode_context::header>{}),
                      "std::indirect payload must encode exactly one CBOR item");
    }
};

} // namespace cbor::tags::codec

namespace cbor::tags::cddl {

template <typename T, typename Alloc> struct wire_type<std::indirect<T, Alloc>> {
    using type = T;
};

} // namespace cbor::tags::cddl
