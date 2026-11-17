#pragma once

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/detail/cbor_resumable_control.h"

#include <concepts>

namespace cbor::tags::detail {

// Make all hook names available for a pack-expanded using declaration without
// adding callable overloads to codecs that omit a direction. Ambiguous lookup
// against the fallback detects names, including overloaded function templates.
struct resumable_hook_names {
    void resumable_encode();
    void resumable_decode();
    void resumable_transcode();
};

template <typename Codec> struct resumable_hook_name_probe : Codec, resumable_hook_names {};

template <typename Codec>
concept NamesResumableEncode = !requires { &resumable_hook_name_probe<Codec>::resumable_encode; };
template <typename Codec>
concept NamesResumableDecode = !requires { &resumable_hook_name_probe<Codec>::resumable_decode; };
template <typename Codec>
concept NamesResumableTranscode = !requires { &resumable_hook_name_probe<Codec>::resumable_transcode; };

template <typename Codec, bool = NamesResumableEncode<Codec>> struct resumable_encode_mixin : Codec {
    void resumable_encode() = delete;
};
template <typename Codec> struct resumable_encode_mixin<Codec, true> : Codec {
    using Codec::resumable_encode;
};

template <typename Codec, bool = NamesResumableDecode<Codec>> struct resumable_decode_mixin : resumable_encode_mixin<Codec> {
    void resumable_decode() = delete;
};
template <typename Codec> struct resumable_decode_mixin<Codec, true> : resumable_encode_mixin<Codec> {
    using Codec::resumable_decode;
};

template <typename Codec, bool = NamesResumableTranscode<Codec>> struct resumable_codec_mixin : resumable_decode_mixin<Codec> {
    void resumable_transcode() = delete;
};
template <typename Codec> struct resumable_codec_mixin<Codec, true> : resumable_decode_mixin<Codec> {
    using Codec::resumable_transcode;
};

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

// Keep ADL outside the context class, whose installed mixins have hook members
// with the same names and would suppress argument-dependent lookup.
template <typename Context, typename T> resumable<status_code> invoke_resumable_encode(Context &context, T &value) {
    return resumable_encode(context, value);
}
template <typename Context, typename T> resumable<status_code> invoke_resumable_decode(Context &context, T &value) {
    return resumable_decode(context, value);
}
template <typename Context, typename T> resumable<status_code> invoke_resumable_transcode(Context &context, T &value) {
    return resumable_transcode(context, value);
}

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

// A built-in match must beat a mixin hook that only fits through a
// conversion, like the one-shot overload probe. An encode hook for double
// would otherwise take every int, and keep a reference to the converted
// temporary. A hook for a base type fits a derived value the same way, in both
// directions, and the one-shot encoder and decoder leave that one alone too.
struct builtin_resumable_overload {};

template <typename Codec> struct resumable_encode_overload_probe : Codec {
    using Codec::resumable_encode;
    template <typename T> builtin_resumable_overload resumable_encode(const volatile T &);
};

template <typename Codec> struct resumable_decode_overload_probe : Codec {
    using Codec::resumable_decode;
    template <typename T> builtin_resumable_overload resumable_decode(const volatile T &);
};

template <typename Codec> struct resumable_transcode_overload_probe : Codec {
    using Codec::resumable_transcode;
    template <typename T> builtin_resumable_overload resumable_transcode(const volatile T &);
};

// The probes only exist for codecs that have the hook, so the first check goes first
template <typename Codec, typename T>
concept HasDirectResumableEncodeMixin =
    HasResumableEncodeMixin<Codec, T> && !requires(resumable_encode_overload_probe<Codec> &probe, T &value) {
        { probe.resumable_encode(value) } -> std::same_as<builtin_resumable_overload>;
    };

template <typename Codec, typename T>
concept HasDirectResumableDecodeMixin =
    HasResumableDecodeMixin<Codec, T> && !requires(resumable_decode_overload_probe<Codec> &probe, T &value) {
        { probe.resumable_decode(value) } -> std::same_as<builtin_resumable_overload>;
    };

template <typename Codec, typename T>
concept HasDirectResumableTranscodeMixin =
    HasResumableTranscodeMixin<Codec, T> && !requires(resumable_transcode_overload_probe<Codec> &probe, T &value) {
        { probe.resumable_transcode(value) } -> std::same_as<builtin_resumable_overload>;
    };

// A hook that is spelled almost right compiles and is never called, and the
// value is then read or written as if it had none. These find such a hook by
// its name, so the encoder and decoder can refuse it. FalseType is Access
// saying the class has no such member.
template <typename Result>
concept UnusableResumableHookResult = !std::same_as<Result, FalseType> && !std::same_as<Result, resumable<status_code>>;

// Converts to T && and nothing else, so a hook for another type can't answer
// through a second conversion
template <typename T> struct resumable_hook_rvalue {
    operator T &&() const noexcept;
};

template <typename Context, typename T>
concept HasRvalueOnlyResumableDecodeFreeFunction = !requires(Context &context, T &value) {
    resumable_decode(context, value);
} && requires(Context &context, resumable_hook_rvalue<T> value) { resumable_decode(context, value); };

template <typename Context, typename T>
concept HasRvalueOnlyResumableTranscodeFreeFunction = !requires(Context &context, T &value) {
    resumable_transcode(context, value);
} && requires(Context &context, resumable_hook_rvalue<T> value) { resumable_transcode(context, value); };

// An encode hook gets a const lvalue, so one for T && is never called either
template <typename Context, typename T>
concept HasRvalueOnlyResumableEncodeFreeFunction = !requires(Context &context, const T &value) {
    resumable_encode(context, value);
} && requires(Context &context, resumable_hook_rvalue<T> value) { resumable_encode(context, value); };

template <typename Context, typename T>
concept HasMismatchedResumableTranscodeHook = requires(Context &context, T &value) {
    { Access::resumable_transcode(context, value) } -> UnusableResumableHookResult;
} || requires(Context &context, T &value) {
    { resumable_transcode(context, value) } -> UnusableResumableHookResult;
} || HasRvalueOnlyResumableTranscodeFreeFunction<Context, T>;

template <typename Context, typename T>
concept HasMismatchedResumableDecodeHook =
    std::is_class_v<T> && (requires(Context &context, T &value) {
        { Access::resumable_decode(context, value) } -> UnusableResumableHookResult;
    } || requires(Context &context, T &value) {
        { resumable_decode(context, value) } -> UnusableResumableHookResult;
    } || HasRvalueOnlyResumableDecodeFreeFunction<Context, T> || HasMismatchedResumableTranscodeHook<Context, T>);

// Access calls an encode member on a const object. A public one that needs a
// mutable object is the non-const member the real concept turns down.
template <typename Context, typename T>
concept HasMutableOnlyResumableEncodeMember = requires(Context &context, T &value) { value.resumable_encode(context); } &&
                                              !requires(Context &context, const T &value) { value.resumable_encode(context); };

template <typename Context, typename T>
concept HasMismatchedResumableEncodeHook =
    std::is_class_v<T> && (requires(Context &context, const T &value) {
        { Access::resumable_encode(context, value) } -> UnusableResumableHookResult;
    } || requires(Context &context, const T &value) {
        { resumable_encode(context, value) } -> UnusableResumableHookResult;
    } || HasRvalueOnlyResumableEncodeFreeFunction<Context, T> || HasMutableOnlyResumableEncodeMember<Context, T> ||
                           HasMismatchedResumableTranscodeHook<Context, T>);

} // namespace cbor::tags::detail
