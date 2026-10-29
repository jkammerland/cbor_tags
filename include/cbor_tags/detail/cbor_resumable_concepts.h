#pragma once

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/detail/cbor_resumable_control.h"

#include <concepts>

namespace cbor::tags::detail {

// Access preserves private member hooks and the const encode-member contract.
template <typename Context, typename T>
concept HasResumableEncodeMethod = requires(Context &context, T &value) {
    { Access::resumable_encode(context, value) } -> std::same_as<resumable<status_code>>;
};

template <typename Context, typename T>
concept HasResumableDecodeMethod = requires(Context &context, T &value) {
    { Access::resumable_decode(context, value) } -> std::same_as<resumable<status_code>>;
};

template <typename Context, typename T>
concept HasResumableTranscodeMethod = requires(Context &context, T &value) {
    { Access::resumable_transcode(context, value) } -> std::same_as<resumable<status_code>>;
};

// ADL and mixin hooks receive the same qualified lvalues as dispatch.
template <typename Context, typename T>
concept HasResumableEncodeFreeFunction = requires(Context &context, T &value) {
    { resumable_encode(context, value) } -> std::same_as<resumable<status_code>>;
};

template <typename Context, typename T>
concept HasResumableDecodeFreeFunction = requires(Context &context, T &value) {
    { resumable_decode(context, value) } -> std::same_as<resumable<status_code>>;
};

template <typename Context, typename T>
concept HasResumableTranscodeFreeFunction = requires(Context &context, T &value) {
    { resumable_transcode(context, value) } -> std::same_as<resumable<status_code>>;
};

template <typename Codec, typename T>
concept HasResumableEncodeMixin = requires(Codec &codec, T &value) {
    { codec.resumable_encode(value) } -> std::same_as<resumable<status_code>>;
};

template <typename Codec, typename T>
concept HasResumableDecodeMixin = requires(Codec &codec, T &value) {
    { codec.resumable_decode(value) } -> std::same_as<resumable<status_code>>;
};

template <typename Codec, typename T>
concept HasResumableTranscodeMixin = requires(Codec &codec, T &value) {
    { codec.resumable_transcode(value) } -> std::same_as<resumable<status_code>>;
};

} // namespace cbor::tags::detail
