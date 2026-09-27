#pragma once

#include "cbor_tags/cbor_extensions.h"

namespace cbor::tags::detail {

struct core_payload_overload {};
struct forwarding_payload_overload {};

// Mirror the core overload signatures that can contain reflected groups, using
// a distinct return type only for unevaluated overload resolution. An exact
// core match must beat a conversion-only or less-specific codec overload.
// Keep these declarations aligned with cbor_encoder.h and cbor_decoder.h,
// including their built-in optional, variant, and indefinite mixin overloads.
template <typename Self, typename Codec> struct payload_encoder_overload_probe : Codec {
    using Codec::encode;

    template <typename T> core_payload_overload        encode(const T &);
    template <IsString T> core_payload_overload        encode(const T &);
    template <IsArray T> core_payload_overload         encode(const T &);
    template <IsMap T> core_payload_overload           encode(const T &);
    template <IsTaggedTuple T> core_payload_overload   encode(const T &);
    template <IsUntaggedTuple T> core_payload_overload encode(const T &);
    template <IsVariant T> core_payload_overload       encode(const T &);
    template <typename T>
        requires(IsAggregate<T> && !IsClassWithEncodingOverload<Self, T> && !HasIncompatibleEncodingCustomization<Self, T>)
    core_payload_overload encode(const T &);

    template <typename T> core_payload_overload                                   encode(const std::optional<T> &);
    template <typename T> core_payload_overload                                   encode(as_indefinite<T>);
    template <IsString T, std::size_t Min, std::size_t Max> core_payload_overload encode(const bounded_size<T, Min, Max> &);
    template <IsArray T, std::size_t Min, std::size_t Max> core_payload_overload  encode(const bounded_size<T, Min, Max> &);
    template <IsMap T, std::size_t Min, std::size_t Max> core_payload_overload    encode(const bounded_size<T, Min, Max> &);
    template <IsString T> core_payload_overload                                   encode(const dynamic_bounded_size<T> &);
    template <IsArray T> core_payload_overload                                    encode(const dynamic_bounded_size<T> &);
    template <IsMap T> core_payload_overload                                      encode(const dynamic_bounded_size<T> &);
};

template <typename Self, typename Codec> struct payload_decoder_overload_probe : Codec {
    using Codec::decode;

    template <typename T> forwarding_payload_overload      decode(T &);
    template <typename T> core_payload_overload            decode(T &, major_type, std::byte);
    template <IsUntaggedTuple T> core_payload_overload     decode(T &);
    template <IsUntaggedTuple T> core_payload_overload     decode(T &, major_type, std::byte);
    template <IsTaggedTuple T> core_payload_overload       decode(T &, major_type, std::byte);
    template <typename T> core_payload_overload            decode(T &, std::uint64_t);
    template <IsTaggedTuple T> core_payload_overload       decode(T &, std::uint64_t);
    template <IsRangeOfCborValues T> core_payload_overload decode(T &, major_type, std::byte);
    template <IsVariant T> core_payload_overload           decode(T &, major_type, std::byte);
    template <typename T>
        requires(IsAggregate<T> && !IsClassWithDecodingOverload<Self, T> && !HasIncompatibleDecodingCustomization<Self, T>)
    core_payload_overload decode(T &);
    template <typename T>
        requires(IsAggregate<T> && !IsClassWithDecodingOverload<Self, T> && !HasIncompatibleDecodingCustomization<Self, T>)
    core_payload_overload decode(T &, major_type, std::byte);
    template <typename T>
        requires(IsAggregate<T> && !IsClassWithDecodingOverload<Self, T> && !HasIncompatibleDecodingCustomization<Self, T>)
    core_payload_overload decode(T &, std::uint64_t);

    template <typename T> core_payload_overload decode(std::optional<T> &, major_type, std::byte);
    template <typename T> core_payload_overload decode(as_indefinite<T>);
    template <typename T> core_payload_overload decode(as_indefinite<T>, major_type, std::byte);
    template <IsBinaryString T, std::size_t Min, std::size_t Max>
    core_payload_overload decode(bounded_size<T, Min, Max> &, major_type, std::byte);
    template <IsTextString T, std::size_t Min, std::size_t Max>
    core_payload_overload                                                        decode(bounded_size<T, Min, Max> &, major_type, std::byte);
    template <IsArray T, std::size_t Min, std::size_t Max> core_payload_overload decode(bounded_size<T, Min, Max> &, major_type, std::byte);
    template <IsMap T, std::size_t Min, std::size_t Max> core_payload_overload   decode(bounded_size<T, Min, Max> &, major_type, std::byte);
    template <IsBinaryString T> core_payload_overload                            decode(dynamic_bounded_size<T> &, major_type, std::byte);
    template <IsTextString T> core_payload_overload                              decode(dynamic_bounded_size<T> &, major_type, std::byte);
    template <IsArray T> core_payload_overload                                   decode(dynamic_bounded_size<T> &, major_type, std::byte);
    template <IsMap T> core_payload_overload                                     decode(dynamic_bounded_size<T> &, major_type, std::byte);
};

} // namespace cbor::tags::detail
