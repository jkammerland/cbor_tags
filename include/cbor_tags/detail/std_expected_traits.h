#pragma once

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_extensions.h"
#include "cbor_tags/cbor_reflection.h"
#include "cbor_tags/detail/cbor_payload_overload.h"
#include "cbor_tags/detail/cbor_variant_traits.h"

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace cbor::tags::ext::std_expected::detail {

template <typename Self> struct expected_payload_mixin_customization;

template <typename Buffer, IsOptions Options, template <typename> typename... Codecs>
struct expected_payload_mixin_customization<encoder<Buffer, Options, Codecs...>> {
    using self_type = encoder<Buffer, Options, Codecs...>;

    template <typename T, typename Codec> static consteval bool accepts_codec() {
        if constexpr (std::is_void_v<T> || !std::is_base_of_v<cbor_encoder_mixin_base<self_type>, Codec>) {
            return false;
        } else {
            using probe = cbor::tags::detail::payload_encoder_overload_probe<self_type, Codec>;
            if constexpr (requires(self_type &self, probe &codec, const T &value) {
                              self.encode(value);
                              codec.encode(value);
                          }) {
                return !std::same_as<decltype(std::declval<probe &>().encode(std::declval<const T &>())),
                                     cbor::tags::detail::core_payload_overload>;
            } else {
                return false;
            }
        }
    }

    template <typename T> static consteval bool accepts() { return (accepts_codec<T, Codecs<self_type>>() || ...); }
};

template <typename Buffer, IsOptions Options, template <typename> typename... Codecs>
struct expected_payload_mixin_customization<decoder<Buffer, Options, Codecs...>> {
    using self_type = decoder<Buffer, Options, Codecs...>;

    template <typename T, typename Codec> static consteval bool accepts_codec() {
        if constexpr (std::is_void_v<T> || !std::is_base_of_v<cbor_decoder_mixin_base<self_type>, Codec>) {
            return false;
        } else {
            using probe = cbor::tags::detail::payload_decoder_overload_probe<self_type, Codec>;
            return requires(self_type &self, probe &codec, T &value) {
                { self.decode(value) } -> std::same_as<status_code>;
                { codec.decode(value) } -> std::same_as<status_code>;
            } || requires(self_type &self, probe &codec, T &value, major_type major, std::byte info) {
                { self.decode(value, major, info) } -> std::same_as<status_code>;
                { codec.decode(value, major, info) } -> std::same_as<status_code>;
            };
        }
    }

    template <typename T> static consteval bool accepts() { return (accepts_codec<T, Codecs<self_type>>() || ...); }
};

template <typename Self, typename T, typename... Parents> consteval bool expected_payload_encodes_one_item();

template <typename Self, typename T> consteval bool expected_payload_has_customization() {
    if constexpr (requires { typename Self::input_buffer_type; }) {
        return IsClassWithDecodingOverload<Self, T> || expected_payload_mixin_customization<Self>::template accepts<T>();
    } else {
        return IsClassWithEncodingOverload<Self, T> || expected_payload_mixin_customization<Self>::template accepts<T>();
    }
}

template <typename Self, typename Tuple, std::size_t Offset, typename... Parents> consteval bool expected_group_encodes_one_item() {
    constexpr auto size = std::tuple_size_v<Tuple>;
    if constexpr (size <= Offset || (size - Offset > 1U && !Self::options::wrap_groups)) {
        return false;
    } else {
        return []<std::size_t... Is>(std::index_sequence<Is...>) {
            return (expected_payload_encodes_one_item<Self, std::tuple_element_t<Offset + Is, Tuple>, Parents...>() && ...);
        }(std::make_index_sequence<size - Offset>{});
    }
}

template <typename Self, typename T, typename... Parents> consteval bool expected_payload_encodes_one_item() {
    using type = std::remove_cvref_t<T>;
    if constexpr ((std::is_same_v<type, Parents> || ...)) {
        // A recursive schema revisits an item shape already checked on this
        // path. Runtime recursion and resource policy remain caller-owned.
        return true;
    } else if constexpr (IsAnyHeader<type> || is_static_tag_t<type>::value || is_dynamic_tag_t<type>) {
        return false;
    } else if constexpr (expected_payload_has_customization<Self, type>()) {
        // Application customizations own their wire representation and must
        // supply one complete item, even when the C++ object has no fields.
        return true;
    } else if constexpr (IsOptional<type>) {
        return expected_payload_encodes_one_item<Self, typename type::value_type, Parents..., type>();
    } else if constexpr (IsBoundedSizeWrapper<type>) {
        if constexpr (type::max_size == 0U) {
            return true;
        } else {
            return expected_payload_encodes_one_item<Self, typename type::value_type, Parents..., type>();
        }
    } else if constexpr (IsDynamicBoundedSizeWrapper<type>) {
        return expected_payload_encodes_one_item<Self, decltype(std::declval<type &>().value()), Parents..., type>();
    } else if constexpr (IsIndefiniteWrapper<type>) {
        return expected_payload_encodes_one_item<Self, indefinite_value_t<type>, Parents..., type>();
    } else if constexpr (IsVariant<type>) {
        return cbor::tags::detail::with_variant_alternatives<type>(
            []<typename... Ts>() { return (expected_payload_encodes_one_item<Self, Ts, Parents..., type>() && ...); });
    } else if constexpr (IsMap<type> && requires {
                             typename type::key_type;
                             typename type::mapped_type;
                         }) {
        return expected_payload_encodes_one_item<Self, typename type::key_type, Parents..., type>() &&
               expected_payload_encodes_one_item<Self, typename type::mapped_type, Parents..., type>();
    } else if constexpr (IsArray<type> && requires { typename type::value_type; }) {
        // A fixed empty array has a complete array header and no child items
        // to validate, even when its element type is an empty group.
        if constexpr (IsFixedArray<type>) {
            if constexpr (requires { typename std::tuple_size<type>::type; }) {
                if constexpr (std::tuple_size_v<type> == 0U) {
                    return true;
                }
            }
        }
        if constexpr (cbor::tags::detail::is_static_extent_span_v<type>) {
            if constexpr (type::extent == 0U) {
                return true;
            }
        }
        return expected_payload_encodes_one_item<Self, typename type::value_type, Parents..., type>();
    } else if constexpr (IsAggregate<type>) {
        using tuple_type      = std::remove_cvref_t<decltype(to_tuple(std::declval<type &>()))>;
        constexpr auto offset = IsTag<type> && !HasInlineTag<type> ? 1U : 0U;
        return expected_group_encodes_one_item<Self, tuple_type, offset, Parents..., type>();
    } else if constexpr (IsTaggedTuple<type> || IsTagOnlyTuple<type>) {
        return expected_group_encodes_one_item<Self, type, 1U, Parents..., type>();
    } else if constexpr (IsUntaggedTuple<type>) {
        return expected_group_encodes_one_item<Self, type, 0U, Parents..., type>();
    } else {
        // Scalar values and opt-in codec types supply their own complete item.
        // Nested expected values enforce this same rule in their codec call.
        return true;
    }
}

} // namespace cbor::tags::ext::std_expected::detail
