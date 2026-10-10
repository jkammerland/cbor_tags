#pragma once

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_reflection.h"
#include "cbor_tags/codec.h"
#include "cbor_tags/detail/cbor_payload_overload.h"
#include "cbor_tags/detail/cbor_variant_traits.h"

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace cbor::tags::detail {

enum class payload_decode_context { direct, header, tag, indefinite };
template <payload_decode_context Context> using payload_context = std::integral_constant<payload_decode_context, Context>;
template <typename T, payload_decode_context Context, bool CoreOnly> struct payload_visit {};
template <typename Self> inline constexpr bool payload_is_decoder = requires { typename Self::input_buffer_type; };

template <typename Self> struct codec_payload_mixin_customization;

template <typename Buffer, IsOptions Options, template <typename> typename... Codecs>
struct codec_payload_mixin_customization<encoder<Buffer, Options, Codecs...>> {
    using self_type = encoder<Buffer, Options, Codecs...>;

    template <typename T, typename Codec> static consteval bool accepts_codec() {
        if constexpr (std::is_void_v<T> || !std::is_base_of_v<cbor::tags::codec::encoder_base<self_type>, Codec>) {
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
struct codec_payload_mixin_customization<decoder<Buffer, Options, Codecs...>> {
    using self_type = decoder<Buffer, Options, Codecs...>;

    template <typename T, payload_decode_context Context, typename Codec> static consteval bool accepts_codec() {
        if constexpr (std::is_void_v<T> || !std::is_base_of_v<cbor::tags::codec::decoder_base<self_type>, Codec>) {
            return false;
        } else {
            using probe                     = cbor::tags::detail::payload_decoder_overload_probe<self_type, Codec>;
            constexpr bool has_header_codec = requires(self_type &self, probe &codec, T &value, major_type major, std::byte info) {
                { self.decode(value, major, info) } -> std::same_as<status_code>;
                { codec.decode(value, major, info) } -> std::same_as<status_code>;
            };
            if constexpr (Context == payload_decode_context::header) {
                return has_header_codec;
            } else if constexpr (Context == payload_decode_context::tag) {
                return requires(self_type &self, probe &codec, T &value, std::uint64_t tag) {
                    { self.decode(value, tag) } -> std::same_as<status_code>;
                    { codec.decode(value, tag) } -> std::same_as<status_code>;
                };
            } else if constexpr (requires(self_type &self, probe &codec, T &value) {
                                     { self.decode(value) } -> std::same_as<status_code>;
                                     { codec.decode(value) } -> std::same_as<status_code>;
                                 }) {
                return true;
            } else {
                // Only the generic one-argument decoder forwards to the
                // header overload. Reflected groups decode their fields directly.
                return has_header_codec &&
                    requires(probe & codec, T & value)
                {
                    {codec.decode(value)}->std::same_as<cbor::tags::detail::forwarding_payload_overload>;
                };
            }
        }
    }

    template <typename T, payload_decode_context Context> static consteval bool accepts() {
        return (accepts_codec<T, Context, Codecs<self_type>>() || ...);
    }
};

template <typename Self, typename T, typename... Parents, payload_decode_context Context = payload_decode_context::direct,
          bool CoreOnly = false>
consteval bool codec_payload_encodes_one_item(payload_context<Context> = {}, std::bool_constant<CoreOnly> = {});

template <typename Self, typename T, payload_decode_context Context> consteval bool codec_payload_has_customization() {
    if constexpr (payload_is_decoder<Self>) {
        return IsClassWithDecodingOverload<Self, T> || codec_payload_mixin_customization<Self>::template accepts<T, Context>();
    } else {
        return IsClassWithEncodingOverload<Self, T> || codec_payload_mixin_customization<Self>::template accepts<T>();
    }
}

template <typename Self, typename Tuple, std::size_t Offset, typename... Parents,
          payload_decode_context Context = payload_decode_context::direct>
consteval bool codec_group_encodes_one_item(payload_context<Context> = {}) {
    constexpr auto size = std::tuple_size_v<Tuple>;
    if constexpr (size <= Offset || (size - Offset > 1U && !Self::options::wrap_groups)) {
        return false;
    } else if constexpr (size - Offset == 1U && Context == payload_decode_context::header) {
        return codec_payload_encodes_one_item<Self, std::tuple_element_t<Offset, Tuple>, Parents...>(payload_context<Context>{});
    } else {
        return []<std::size_t... Is>(std::index_sequence<Is...>) {
            return (codec_payload_encodes_one_item<Self, std::tuple_element_t<Offset + Is, Tuple>, Parents...>() && ...);
        }(std::make_index_sequence<size - Offset>{});
    }
}

template <typename Self, typename T, bool Fixed, typename... Parents, payload_decode_context Context>
consteval bool codec_range_child_encodes_one_item(payload_context<Context>) {
    if constexpr (!payload_is_decoder<Self> || Fixed) {
        return codec_payload_encodes_one_item<Self, T, Parents...>();
    } else if constexpr (Context == payload_decode_context::indefinite) {
        return codec_payload_encodes_one_item<Self, T, Parents...>(payload_context<payload_decode_context::header>{});
    } else {
        // Ordinary dynamic ranges accept both definite and indefinite forms.
        return codec_payload_encodes_one_item<Self, T, Parents...>() &&
               codec_payload_encodes_one_item<Self, T, Parents...>(payload_context<payload_decode_context::header>{});
    }
}

template <typename Self, typename T, typename... Parents, payload_decode_context Context, bool CoreOnly>
consteval bool codec_payload_encodes_one_item(payload_context<Context>, std::bool_constant<CoreOnly>) {
    using type                       = std::remove_cvref_t<T>;
    constexpr auto effective_context = payload_is_decoder<Self> ? Context : payload_decode_context::direct;
    using visit                      = payload_visit<type, effective_context, CoreOnly>;
    if constexpr ((std::is_same_v<visit, Parents> || ...)) {
        // Only a revisit through the same dispatch route is already checked.
        // Runtime recursion and resource policy remain caller-owned.
        return true;
    } else if constexpr (IsAnyHeader<type> || is_static_tag_t<type>::value || is_dynamic_tag_t<type>) {
        return false;
    } else if constexpr (!CoreOnly && codec_payload_has_customization<Self, type, Context>()) {
        // Application customizations own the item selected on this route.
        return true;
    } else if constexpr (IsOptional<type>) {
        return codec_payload_encodes_one_item<Self, typename type::value_type, Parents..., visit>(
            payload_context<payload_decode_context::header>{});
    } else if constexpr (IsBoundedSizeWrapper<type>) {
        if constexpr (type::max_size == 0U) {
            return true;
        } else {
            // Bounded decoding traverses the underlying range directly;
            // encoding delegates to its ordinary encode overload.
            return codec_payload_encodes_one_item<Self, typename type::value_type, Parents..., visit>(
                payload_context<payload_decode_context::direct>{}, std::bool_constant<payload_is_decoder<Self>>{});
        }
    } else if constexpr (IsDynamicBoundedSizeWrapper<type>) {
        return codec_payload_encodes_one_item<Self, decltype(std::declval<type &>().value()), Parents..., visit>(
            payload_context<payload_decode_context::direct>{}, std::bool_constant<payload_is_decoder<Self>>{});
    } else if constexpr (IsIndefiniteWrapper<type>) {
        // Indefinite arrays/maps iterate the wrapped range without dispatching
        // its own codec. Their decoder passes consumed headers to each child.
        return codec_payload_encodes_one_item<Self, indefinite_value_t<type>, Parents..., visit>(
            payload_context<payload_decode_context::indefinite>{}, std::true_type{});
    } else if constexpr (IsVariant<type>) {
        return cbor::tags::detail::with_variant_alternatives<type>([]<typename... Ts>() {
            return (codec_payload_encodes_one_item<Self, Ts, Parents..., visit>(
                        payload_context < payload_is_decoder<Self> && IsTag<Ts> ? payload_decode_context::tag
                                                                                : payload_decode_context::header > {},
                        std::bool_constant < payload_is_decoder<Self> && IsVariant < Ts >> {}) &&
                    ...);
        });
    } else if constexpr (IsMap<type> && requires {
                             typename type::key_type;
                             typename type::mapped_type;
                         }) {
        return codec_range_child_encodes_one_item<Self, typename type::key_type, false, Parents..., visit>(payload_context<Context>{}) &&
               codec_range_child_encodes_one_item<Self, typename type::mapped_type, false, Parents..., visit>(payload_context<Context>{});
    } else if constexpr (IsArray<type> && requires { typename type::value_type; }) {
        // Fixed empty arrays/spans have no child items to validate.
        if constexpr (IsFixedArray<type>) {
            if constexpr (cbor::tags::detail::is_static_array<type>::value) {
                if constexpr (cbor::tags::detail::is_static_array<type>::extent == 0U) {
                    return true;
                }
            }
        }
        if constexpr (cbor::tags::detail::is_static_extent_span_v<type>) {
            if constexpr (type::extent == 0U) {
                return true;
            }
        }
        return codec_range_child_encodes_one_item<Self, typename type::value_type, IsFixedArray<type>, Parents..., visit>(
            payload_context<Context>{});
    } else if constexpr (IsAggregate<type>) {
        using tuple_type              = std::remove_cvref_t<decltype(to_tuple(std::declval<type &>()))>;
        constexpr auto offset         = IsTag<type> && !HasInlineTag<type> ? 1U : 0U;
        constexpr auto fields_context = IsTag<type> ? payload_decode_context::direct : Context;
        return codec_group_encodes_one_item<Self, tuple_type, offset, Parents..., visit>(payload_context<fields_context>{});
    } else if constexpr (IsTaggedTuple<type> || IsTagOnlyTuple<type>) {
        return codec_group_encodes_one_item<Self, type, 1U, Parents..., visit>();
    } else if constexpr (IsUntaggedTuple<type>) {
        return codec_group_encodes_one_item<Self, type, 0U, Parents..., visit>(payload_context<Context>{});
    } else {
        // Scalar values and opt-in codec types supply their own complete item.
        // Nested expected and indirect values enforce this rule in their codec call.
        return true;
    }
}

} // namespace cbor::tags::detail
