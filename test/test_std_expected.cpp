#if __has_include(<expected>)
#include <expected>
#endif

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L

#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/std_expected.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace cbor::tags;
using namespace cbor::tags::ext::std_expected;

namespace {

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

template <typename T, typename E> void check_decode_error(const char *hex, status_code expected_status) {
    const auto          bytes = to_bytes(hex);
    std::expected<T, E> decoded{};
    auto                dec    = make_decoder<std_expected_codec>(bytes);
    const auto          result = dec(decoded);

    CAPTURE(hex);
    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), expected_status);
}

} // namespace

TEST_CASE("std::expected codec is explicit opt in") {
    using expected_type     = std::expected<std::uint64_t, std::string>;
    using default_encoder   = decltype(make_encoder(std::declval<std::vector<std::byte> &>()));
    using extension_encoder = decltype(make_encoder<std_expected_codec>(std::declval<std::vector<std::byte> &>()));
    using default_decoder   = decltype(make_decoder(std::declval<std::vector<std::byte> &>()));
    using extension_decoder = decltype(make_decoder<std_expected_codec>(std::declval<std::vector<std::byte> &>()));

    static_assert(!CanEncode<default_encoder, expected_type>);
    static_assert(CanEncode<extension_encoder, expected_type>);
    static_assert(!CanDecode<default_decoder, expected_type>);
    static_assert(CanDecode<extension_decoder, expected_type>);
}

TEST_CASE("std::expected codec roundtrips value and error alternatives") {
    {
        const std::expected<std::uint64_t, std::string> value{42U};
        const auto                                      encoded = encode_expected(value);

        CHECK_EQ(to_hex(encoded), "82f5182a");

        const auto decoded = decode_expected<std::uint64_t, std::string>(encoded);
        REQUIRE(decoded.has_value());
        CHECK_EQ(*decoded, 42U);
    }

    {
        const std::expected<std::uint64_t, std::string> value{std::unexpected<std::string>{"bad"}};
        const auto                                      encoded = encode_expected(value);

        CHECK_EQ(to_hex(encoded), "82f463626164");

        const auto decoded = decode_expected<std::uint64_t, std::string>(encoded);
        REQUIRE_FALSE(decoded.has_value());
        CHECK_EQ(decoded.error(), "bad");
    }
}

TEST_CASE("std::expected codec supports void success payloads") {
    {
        const std::expected<void, std::string> value{};
        const auto                             encoded = encode_expected(value);

        CHECK_EQ(to_hex(encoded), "82f5f6");

        std::expected<void, std::string> decoded{std::unexpected<std::string>{"before"}};
        auto                             dec = make_decoder<std_expected_codec>(encoded);
        REQUIRE(dec(decoded));
        CHECK(decoded.has_value());
    }

    {
        const std::expected<void, std::string> value{std::unexpected<std::string>{"failed"}};
        const auto                             encoded = encode_expected(value);

        CHECK_EQ(to_hex(encoded), "82f4666661696c6564");

        std::expected<void, std::string> decoded{};
        auto                             dec = make_decoder<std_expected_codec>(encoded);
        REQUIRE(dec(decoded));
        REQUIRE_FALSE(decoded.has_value());
        CHECK_EQ(decoded.error(), "failed");
    }
}

TEST_CASE("std::expected codec composes with nested expected and optional payloads") {
    {
        using inner_type = std::expected<int, std::string>;
        using outer_type = std::expected<inner_type, std::string>;

        const outer_type value{inner_type{std::unexpected<std::string>{"inner"}}};
        const auto       encoded = encode_expected(value);

        const auto decoded = decode_expected<inner_type, std::string>(encoded);
        REQUIRE(decoded.has_value());
        REQUIRE_FALSE(decoded->has_value());
        CHECK_EQ(decoded->error(), "inner");
    }

    {
        using expected_type = std::expected<std::optional<int>, std::string>;

        const expected_type value{std::optional<int>{}};
        const auto          encoded = encode_expected(value);

        const auto decoded = decode_expected<std::optional<int>, std::string>(encoded);
        REQUIRE(decoded.has_value());
        CHECK_FALSE(decoded->has_value());
    }
}

TEST_CASE("std::expected codec composes inside aggregate fields") {
    const expected_holder value{7U, std::unexpected<std::uint64_t>{99U}};

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder<std_expected_codec>(buffer);
    REQUIRE(enc(value));

    expected_holder decoded{};
    auto            dec = make_decoder<std_expected_codec>(buffer);
    REQUIRE(dec(decoded));
    CHECK_EQ(decoded.id, 7U);
    REQUIRE_FALSE(decoded.result.has_value());
    CHECK_EQ(decoded.result.error(), 99U);
}

TEST_CASE("std::expected codec decodes from non-contiguous buffers") {
    const std::expected<std::string, std::uint64_t> value{std::string{"ok"}};
    const auto                                      encoded = encode_expected(value);
    const std::deque<std::byte>                     input(encoded.begin(), encoded.end());

    std::expected<std::string, std::uint64_t> decoded;
    auto                                      dec = make_decoder<std_expected_codec>(input);
    REQUIRE(dec(decoded));
    REQUIRE(decoded.has_value());
    CHECK_EQ(*decoded, "ok");
}

TEST_CASE("std::expected codec accepts indefinite two-item arrays") {
    const auto bytes = to_bytes("9ff5182aff");

    std::expected<int, std::string> decoded{};
    auto                            dec = make_decoder<std_expected_codec>(bytes);

    REQUIRE(dec(decoded));
    REQUIRE(decoded.has_value());
    CHECK_EQ(*decoded, 42);
}

TEST_CASE("std::expected codec reports malformed wrappers and payloads") {
    check_decode_error<std::uint64_t, std::string>("a0", status_code::no_match_for_array_on_buffer);
    check_decode_error<std::uint64_t, std::string>("81f5", status_code::unexpected_group_size);
    check_decode_error<std::uint64_t, std::string>("83f5182a00", status_code::unexpected_group_size);
    check_decode_error<std::uint64_t, std::string>("8200182a", status_code::no_match_for_simple_on_buffer);
    check_decode_error<std::uint64_t, std::string>("82f563626164", status_code::no_match_for_uint_on_buffer);
    check_decode_error<std::uint64_t, std::string>("82f4182a", status_code::no_match_for_tstr_on_buffer);
    check_decode_error<std::uint64_t, std::string>("9ff5182a00ff", status_code::unexpected_group_size);
}

#endif
