#pragma once

#include "cbor_tags/detail/cbor_item_shape.h"

#include <concepts>
#include <type_traits>

namespace cbor::tags {

// A declaration covers the complete encode or decode call, including nested calls
// and external effects. Codec is void for member/ADL customizations; mixins use
// the Codec<Transcoder> specialization that performs the retried call.
template <typename T, typename Codec = void> struct resumable_retry_traits {
    static constexpr bool encode = false;
    static constexpr bool decode = false;
};

namespace detail {

template <typename Codec, typename T>
concept RetryDecodeRvalue = requires(Codec &codec, T &value) { codec.decode(std::move(value)); };
template <typename Codec, typename T>
concept RetryDecodeDirect = requires(Codec &codec, T &value) { codec.decode(value); };
template <typename Codec, typename T>
concept RetryDecodeHeader = requires(Codec &codec, T &value) { codec.decode(value, major_type{}, std::byte{}); };
template <typename Codec, typename T>
concept RetryDecodeTag = requires(Codec &codec, T &value) { codec.decode(value, std::uint64_t{}); };
template <typename Codec, typename T>
concept RetryEncodeRvalue = requires(Codec &codec, T &value) { codec.encode(std::move(value)); };
template <typename Codec, typename T>
concept RetryEncodeDirect = requires(Codec &codec, T &value) { codec.encode(value); };

template <typename Codec>
concept RetryDecodeObserver = requires(Codec &codec) { codec.observe_decoded_cbor_tag(std::uint64_t{}); };
template <typename Codec>
concept RetryEncodeObserver = requires(Codec &codec) { codec.observe_encoded_cbor_tag(std::uint64_t{}); };

// What a declaration says for one direction, a flag it leaves out is false.
// Only the flag that was asked for gets looked at.
template <typename T, typename Codec, bool Encode> consteval bool resumable_retry_declared() {
    using traits = resumable_retry_traits<std::remove_cvref_t<T>, Codec>;

#if defined(__clang__)
    // A declaration for a file-local type that only ever goes one way makes
    // Clang warn about the flag nobody named, so name both here. Not for the
    // others: naming a flag can be enough to have it worked out, GCC 12 does
    // that, and the flag for the other way may not have an answer
    [[maybe_unused]] constexpr bool named = requires { traits::encode; } || requires { traits::decode; };
#endif

    if constexpr (Encode) {
        if constexpr (requires { traits::encode; })
            return static_cast<bool>(traits::encode);
    } else {
        if constexpr (requires { traits::decode; })
            return static_cast<bool>(traits::decode);
    }

    return false;
}

// Probe each installed mixin separately. A value's declaration must never
// authorize an unrelated codec override, including overrides of built-in types.
template <typename Transcoder, typename T, typename Codec, payload_decode_context Context>
consteval bool resumable_codec_decode_retry_allowed() {
    constexpr bool custom = [] {
        if constexpr (std::is_base_of_v<codec::decoder_base<Transcoder>, Codec>) {
            return codec_payload_mixin_customization<Transcoder>::template accepts_codec<T, Context, Codec>() ||
                   (Context == payload_decode_context::direct && RetryDecodeRvalue<Codec, T>);
        } else if constexpr (Context == payload_decode_context::tag) {
            return RetryDecodeTag<Codec, T>;
        } else if constexpr (Context == payload_decode_context::header) {
            return RetryDecodeHeader<Codec, T>;
        } else {
            return RetryDecodeDirect<Codec, T> || RetryDecodeHeader<Codec, T>;
        }
    }();
    return (!custom && !RetryDecodeObserver<Codec>) || resumable_retry_declared<T, Codec, false>();
}

template <typename Transcoder, typename T, payload_decode_context Context, typename... Codecs>
consteval bool resumable_decode_retry_allowed() {
    constexpr bool allowed = (!IsClassWithDecodingOverload<Transcoder, T> || resumable_retry_declared<T, void, false>()) &&
                             (resumable_codec_decode_retry_allowed<Transcoder, T, Codecs, Context>() && ...);
    if constexpr (IsEnum<T>) {
        using customization = codec_payload_mixin_customization<Transcoder>;
        if constexpr (!IsClassWithDecodingOverload<Transcoder, T> && !customization::template starts_on_first_byte<T>()) {
            // Built-in enum decoding calls the underlying integer's codec.
            // A whole-enum override instead owns its complete retry contract.
            using underlying    = std::underlying_type_t<T>;
            constexpr auto form = customization::template wants_first_byte<underlying, payload_decode_context::header>()
                                      ? payload_decode_context::direct
                                      : payload_decode_context::header;
            return allowed && ((!customization::template codec_overload_selected<underlying, form, Codecs>() ||
                                resumable_retry_declared<underlying, Codecs, false>()) &&
                               ...);
        }
    }
    return allowed;
}

template <typename Transcoder, typename T, typename Codec> consteval bool resumable_codec_encode_customized() {
    if constexpr (std::is_base_of_v<codec::encoder_base<Transcoder>, Codec>) {
        using probe = payload_encoder_overload_probe<Transcoder, Codec>;
        if constexpr (RetryEncodeRvalue<probe, T>) {
            return codec_payload_mixin_customization<Transcoder>::template accepts_codec<T, Codec>() ||
                   !std::same_as<decltype(std::declval<probe &>().encode(std::declval<T &&>())), core_payload_overload>;
        } else {
            return codec_payload_mixin_customization<Transcoder>::template accepts_codec<T, Codec>();
        }
    } else {
        return RetryEncodeDirect<Codec, T>;
    }
}

template <typename Transcoder, typename T, typename Codec> consteval bool resumable_codec_encode_retry_allowed() {
    return (!resumable_codec_encode_customized<Transcoder, T, Codec>() && !RetryEncodeObserver<Codec>) ||
           resumable_retry_declared<T, Codec, true>();
}

template <typename T> inline constexpr bool    resumable_retry_wrap_array                      = false;
template <typename... T> inline constexpr bool resumable_retry_wrap_array<wrap_as_array<T...>> = true;

// Built-in delegation does not establish a customization's whole-call contract.
// Follow its children until a declared customization takes ownership. Remember
// visited types so recursive application schemas remain finite at compile time.
template <typename Transcoder, typename... Codecs> struct resumable_encode_retry {
    template <typename Object, typename... Parents> static consteval bool named_fields() {
        using tuple = std::remove_cvref_t<decltype(to_tuple(std::declval<const Object &>()))>;
        return []<std::size_t... I>(std::index_sequence<I...>) {
            return (named_field<std::remove_cvref_t<std::tuple_element_t<I, tuple>>, Parents...>() && ...);
        }(std::make_index_sequence<std::tuple_size_v<tuple>>{});
    }

    template <typename Field, typename... Parents> static consteval bool named_field() {
        if constexpr (IsNamedGroupWrapper<Field>)
            return named_fields<named_group_value_t<Field>, Parents...>();
        else if constexpr (IsNamedExtensionWrapper<Field>)
            return elements<const named_extension_value_t<Field>, true, Parents...>();
        else if constexpr (IsOptional<Field>)
            return allowed<std::string_view, false, Parents...>() && allowed<const typename Field::value_type, false, Parents...>();
        else
            return allowed<std::string_view, false, Parents...>() && allowed<const Field, false, Parents...>();
    }

    template <typename Tuple, typename... Parents> static consteval bool fields() {
        return []<std::size_t... I>(std::index_sequence<I...>) {
            return (allowed<const std::remove_reference_t<std::tuple_element_t<I, Tuple>>, false, Parents...>() && ...);
        }(std::make_index_sequence<std::tuple_size_v<Tuple>>{});
    }

    template <typename Range, bool Map, typename... Parents> static consteval bool elements() {
        using element = const std::remove_reference_t<std::ranges::range_reference_t<Range>> &;
        if constexpr (Map) {
            return allowed<std::remove_reference_t<decltype(pair_first(std::declval<element>()))>, false, Parents...>() &&
                   allowed<std::remove_reference_t<decltype(pair_second(std::declval<element>()))>, false, Parents...>();
        } else {
            return allowed<std::remove_reference_t<element>, false, Parents...>();
        }
    }

    template <typename T, bool CoreOnly = false, typename... Parents> static consteval bool allowed() {
        using U               = std::remove_cvref_t<T>;
        using visit           = std::pair<T, std::bool_constant<CoreOnly>>;
        constexpr bool custom = !CoreOnly && (IsClassWithEncodingOverload<Transcoder, T> ||
                                              (resumable_codec_encode_customized<Transcoder, T, Codecs>() || ...));
        // Distinct dispatch boundaries intentionally share the same retry verdict.
        // NOLINTNEXTLINE(bugprone-branch-clone)
        if constexpr ((std::same_as<visit, Parents> || ...)) {
            return true;
        } else if constexpr (!CoreOnly && ((!resumable_codec_encode_retry_allowed<Transcoder, T, Codecs>() || ...) ||
                                           (IsClassWithEncodingOverload<Transcoder, T> && !resumable_retry_declared<T, void, true>()))) {
            return false;
        } else if constexpr (custom) {
            // Its declaration includes every call made by the customization.
            return true;
        } else if constexpr (IsEnum<U>) {
            return allowed<std::underlying_type_t<U>, false, Parents..., visit>();
        } else if constexpr (std::same_as<U, integer>) {
            return allowed<positive, false, Parents..., visit>() && allowed<negative, false, Parents..., visit>();
        } else if constexpr (IsAnyBoundedSizeWrapper<U>) {
            return allowed<std::remove_reference_t<decltype(std::declval<const U &>().value())>, false, Parents..., visit>();
        } else if constexpr (IsIndefiniteWrapper<U>) {
            // Indefinite containers iterate directly; strings dispatch the wrapped value.
            using wrapped = indefinite_value_t<U>;
            return allowed<wrapped, !IsString<wrapped>, Parents..., visit>();
        } else if constexpr (resumable_retry_wrap_array<U>) {
            return fields<std::remove_cvref_t<decltype(std::declval<U>().values_)>, Parents..., visit>();
        } else if constexpr (IsNamedMapWrapper<U>) {
            return named_fields<named_map_value_t<U>, Parents..., visit>();
        } else if constexpr (IsArrayRangeWrapper<U> || IsMapRangeWrapper<U>) {
            using range   = decltype((std::declval<T &>().range_));
            using element = std::ranges::range_reference_t<range>;
            if constexpr (IsMapRangeWrapper<U>) {
                return allowed<std::remove_reference_t<decltype(pair_first(std::declval<element &>()))>, false, Parents..., visit>() &&
                       allowed<std::remove_reference_t<decltype(pair_second(std::declval<element &>()))>, false, Parents..., visit>();
            } else if constexpr (CborRangeComponent<element>) {
                return allowed<std::remove_reference_t<element>, false, Parents..., visit>();
            } else {
                return allowed<std::ranges::range_value_t<range>, false, Parents..., visit>();
            }
        } else if constexpr (IsString<U> || IsStringRangeWrapper<U> || IsEncodedItemView<U> || IsAnyHeader<U> ||
                             is_static_tag_t<U>::value || is_dynamic_tag_t<U>) {
            // Payload bytes and headers are written without child codec dispatch.
            return true;
        } else if constexpr (IsOptional<U>) {
            return allowed<const typename U::value_type, false, Parents..., visit>();
        } else if constexpr (IsVariant<U>) {
            return with_variant_alternatives<U>([]<typename... V>() { return (allowed<const V, false, Parents..., visit>() && ...); });
        } else if constexpr (IsArray<U> || IsMap<U>) {
            return elements<const U, IsMap<U>, Parents..., visit>();
        } else if constexpr (IsTaggedTuple<U> || IsUntaggedTuple<U>) {
            return fields<U, Parents..., visit>();
        } else if constexpr (IsAggregate<U>) {
            using tuple = std::remove_cvref_t<decltype(to_tuple(std::declval<const U &>()))>;
            return fields<tuple, Parents..., visit>();
        } else {
            return true;
        }
    }
};

template <typename Transcoder, typename T, typename... Codecs> consteval bool resumable_encode_retry_allowed() {
    return resumable_encode_retry<Transcoder, Codecs...>::template allowed<T>();
}

} // namespace detail
} // namespace cbor::tags
