#pragma once

#include "cbor_tags/detail/cbor_item_shape.h"

#include <concepts>
#include <type_traits>

namespace cbor::tags {

// A declaration covers the complete one-shot call, including nested calls and
// external effects. Codec is void for member/ADL customizations; mixins use the
// actual one-shot Codec<Transcoder> specialization selected by the factory.
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
template <typename OneShot, typename T, typename Codec, payload_decode_context Context>
consteval bool resumable_codec_decode_retry_allowed() {
    constexpr bool custom = [] {
        if constexpr (std::is_base_of_v<codec::decoder_base<OneShot>, Codec>) {
            return codec_payload_mixin_customization<OneShot>::template accepts_codec<T, Context, Codec>() ||
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

template <typename OneShot, typename T, payload_decode_context Context, typename... Codecs>
consteval bool resumable_decode_retry_allowed() {
    constexpr bool allowed = (!IsClassWithDecodingOverload<OneShot, T> || resumable_retry_declared<T, void, false>()) &&
                             (resumable_codec_decode_retry_allowed<OneShot, T, Codecs, Context>() && ...);
    if constexpr (IsEnum<T>) {
        using customization = codec_payload_mixin_customization<OneShot>;
        if constexpr (!IsClassWithDecodingOverload<OneShot, T> && !customization::template starts_on_first_byte<T>()) {
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

template <typename OneShot, typename T, typename Codec> consteval bool resumable_codec_encode_retry_allowed() {
    constexpr bool custom = [] {
        if constexpr (std::is_base_of_v<codec::encoder_base<OneShot>, Codec>) {
            using probe = payload_encoder_overload_probe<OneShot, Codec>;
            if constexpr (RetryEncodeRvalue<probe, T>) {
                return codec_payload_mixin_customization<OneShot>::template accepts_codec<T, Codec>() ||
                       !std::same_as<decltype(std::declval<probe &>().encode(std::declval<T &&>())), core_payload_overload>;
            } else {
                return codec_payload_mixin_customization<OneShot>::template accepts_codec<T, Codec>();
            }
        } else {
            return RetryEncodeDirect<Codec, T>;
        }
    }();
    return (!custom && !RetryEncodeObserver<Codec>) || resumable_retry_declared<T, Codec, true>();
}

template <typename OneShot, typename T, typename... Codecs> consteval bool resumable_encode_retry_allowed() {
    return (!IsClassWithEncodingOverload<OneShot, T> || resumable_retry_declared<T, void, true>()) &&
           (resumable_codec_encode_retry_allowed<OneShot, T, Codecs>() && ...);
}

} // namespace detail
} // namespace cbor::tags
