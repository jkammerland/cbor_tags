#pragma once

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/std_expected.h>
#include <cstdint>
#include <doctest/doctest.h>
#include <expected>
#include <string>
#include <vector>

namespace std_expected_test {

using namespace cbor::tags;
using namespace cbor::tags::ext::std_expected;

template <typename Enc, typename T>
concept CanEncode = requires(Enc &enc, const T &value) { enc.encode(value); };

template <typename Dec, typename T>
concept CanDecode = requires(Dec &dec, T &value, major_type major, std::byte additional_info) {
    { dec.decode(value, major, additional_info) } -> std::same_as<status_code>;
};

struct expected_holder {
    std::uint64_t                             id{};
    std::expected<std::string, std::uint64_t> result{};
};

struct expected_point {
    int x{};
    int y{};

    bool operator==(const expected_point &) const = default;
};

struct customized_empty {
    template <typename Encoder> auto encode(Encoder &enc) const { return enc(nullptr); }
    template <typename Decoder> auto decode(Decoder &dec) {
        std::nullptr_t payload{};
        return dec(payload);
    }
};

struct recursive_expected_payload {
    int                                     id{};
    std::vector<recursive_expected_payload> children;

    bool operator==(const recursive_expected_payload &) const = default;
};

struct empty_expected_element {};

struct mixin_empty {
    bool operator==(const mixin_empty &) const = default;
};

template <typename Self> struct empty_item_codec : cbor_codec_mixin_base<Self> {
    using cbor_codec_mixin_base<Self>::encode;
    using cbor_codec_mixin_base<Self>::decode;

    void        encode(const mixin_empty &) { static_cast<Self &>(*this).encode(nullptr); }
    status_code decode(mixin_empty &) {
        std::nullptr_t payload{};
        return static_cast<Self &>(*this).decode(payload);
    }
    status_code decode(mixin_empty &, major_type major, std::byte info) {
        std::nullptr_t payload{};
        return static_cast<Self &>(*this).decode(payload, major, info);
    }
};

template <typename T> struct templated_mixin_empty {};

template <typename Self> struct templated_empty_item_codec : cbor_codec_mixin_base<Self> {
    using cbor_codec_mixin_base<Self>::encode;
    using cbor_codec_mixin_base<Self>::decode;
    template <typename T> void        encode(const templated_mixin_empty<T> &) { static_cast<Self &>(*this).encode(nullptr); }
    template <typename T> status_code decode(templated_mixin_empty<T> &) {
        std::nullptr_t value{};
        return static_cast<Self &>(*this).decode(value);
    }
    template <typename T> status_code decode(templated_mixin_empty<T> &, major_type major, std::byte info) {
        std::nullptr_t value{};
        return static_cast<Self &>(*this).decode(value, major, info);
    }
};

struct by_value_mixin_empty {};

template <typename Self> struct by_value_empty_item_codec : cbor_codec_mixin_base<Self> {
    using cbor_codec_mixin_base<Self>::encode;
    using cbor_codec_mixin_base<Self>::decode;
    void        encode(by_value_mixin_empty) { static_cast<Self &>(*this).encode(nullptr); }
    status_code decode(by_value_mixin_empty &) {
        std::nullptr_t value{};
        return static_cast<Self &>(*this).decode(value);
    }
    status_code decode(by_value_mixin_empty &, major_type major, std::byte info) {
        std::nullptr_t value{};
        return static_cast<Self &>(*this).decode(value, major, info);
    }
};

template <typename Self> struct constrained_array_item_codec : cbor_codec_mixin_base<Self> {
    using cbor_codec_mixin_base<Self>::encode;
    using cbor_codec_mixin_base<Self>::decode;
    template <IsArray T>
        requires std::same_as<T, std::vector<mixin_empty>>
    void encode(const T &value) {
        static_cast<Self &>(*this).encode(value.size());
    }
    template <IsRangeOfCborValues T>
        requires std::same_as<T, std::vector<mixin_empty>>
    status_code decode(T &value, major_type major, std::byte info) {
        std::size_t size{};
        const auto  status = static_cast<Self &>(*this).decode(size, major, info);
        if (status == status_code::success) {
            value.resize(size);
        }
        return status;
    }
};

template <typename T, typename E> std::vector<std::byte> encode_expected(const std::expected<T, E> &value) {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder<std_expected_codec>(buffer);
    REQUIRE(enc(value));
    return buffer;
}

template <typename T, typename E> std::expected<T, E> decode_expected(const std::vector<std::byte> &buffer) {
    std::expected<T, E> decoded{};
    auto                dec = make_decoder<std_expected_codec>(buffer);
    REQUIRE(dec(decoded));
    return decoded;
}

} // namespace std_expected_test
