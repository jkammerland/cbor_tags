#pragma once

#include "cbor_tags/detail/cbor_item_shape.h"

namespace cbor::tags {

// A declaration covers the complete ordinary call, including nested calls and
// external effects. Codec is void for member/ADL customizations; mixins use the
// actual ordinary Codec<Transcoder> specialization selected by the factory.
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

// Probe each installed mixin separately. A value's declaration must never
// authorize an unrelated codec override, including overrides of built-in types.
template <typename Ordinary, typename T, typename Codec, payload_decode_context Context>
consteval bool resumable_codec_decode_retry_allowed() {
    constexpr bool custom = [] {
        if constexpr (std::is_base_of_v<codec::decoder_base<Ordinary>, Codec>) {
            return codec_payload_mixin_customization<Ordinary>::template accepts_codec<T, Context, Codec>() ||
                   (Context == payload_decode_context::direct && RetryDecodeRvalue<Codec, T>);
        } else if constexpr (Context == payload_decode_context::tag) {
            return RetryDecodeTag<Codec, T>;
        } else if constexpr (Context == payload_decode_context::header) {
            return RetryDecodeHeader<Codec, T>;
        } else {
            return RetryDecodeDirect<Codec, T> || RetryDecodeHeader<Codec, T>;
        }
    }();
    return (!custom && !RetryDecodeObserver<Codec>) || resumable_retry_traits<std::remove_cvref_t<T>, Codec>::decode;
}

template <typename Ordinary, typename T, payload_decode_context Context, typename... Codecs>
consteval bool resumable_decode_retry_allowed() {
    return (!IsClassWithDecodingOverload<Ordinary, T> || resumable_retry_traits<std::remove_cvref_t<T>>::decode) &&
           (resumable_codec_decode_retry_allowed<Ordinary, T, Codecs, Context>() && ...);
}

template <typename Ordinary, typename T, typename Codec> consteval bool resumable_codec_encode_retry_allowed() {
    constexpr bool custom = [] {
        if constexpr (std::is_base_of_v<codec::encoder_base<Ordinary>, Codec>) {
            using probe = payload_encoder_overload_probe<Ordinary, Codec>;
            if constexpr (RetryEncodeRvalue<probe, T>) {
                return codec_payload_mixin_customization<Ordinary>::template accepts_codec<T, Codec>() ||
                       !std::same_as<decltype(std::declval<probe &>().encode(std::declval<T &&>())), core_payload_overload>;
            } else {
                return codec_payload_mixin_customization<Ordinary>::template accepts_codec<T, Codec>();
            }
        } else {
            return RetryEncodeDirect<Codec, T>;
        }
    }();
    return (!custom && !RetryEncodeObserver<Codec>) || resumable_retry_traits<std::remove_cvref_t<T>, Codec>::encode;
}

template <typename Ordinary, typename T, typename... Codecs> consteval bool resumable_encode_retry_allowed() {
    return (!IsClassWithEncodingOverload<Ordinary, T> || resumable_retry_traits<std::remove_cvref_t<T>>::encode) &&
           (resumable_codec_encode_retry_allowed<Ordinary, T, Codecs>() && ...);
}

} // namespace detail
} // namespace cbor::tags
