#if __has_include(<version>)
#include <version>
#endif

#if __has_include(<expected>) && defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
#include "std_expected_test_types.h"

#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/std_expected.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <expected>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace cbor::tags;
using namespace cbor::tags::ext::std_expected;

using namespace std_expected_test;

TEST_SUITE("roundtrip/std_expected") {

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

            const auto decoded = decode_expected<std::uint64_t, std::string>(encoded);
            REQUIRE(decoded.has_value());
            CHECK_EQ(*decoded, 42U);
        }

        {
            const std::expected<std::uint64_t, std::string> value{std::unexpected<std::string>{"bad"}};
            const auto                                      encoded = encode_expected(value);

            const auto decoded = decode_expected<std::uint64_t, std::string>(encoded);
            REQUIRE_FALSE(decoded.has_value());
            CHECK_EQ(decoded.error(), "bad");
        }
    }

    TEST_CASE("std::expected codec supports void success payloads") {
        {
            const std::expected<void, std::string> value{};
            const auto                             encoded = encode_expected(value);

            std::expected<void, std::string> decoded{std::unexpected<std::string>{"before"}};
            auto                             dec = make_decoder<std_expected_codec>(encoded);
            REQUIRE(dec(decoded));
            CHECK(decoded.has_value());
        }

        {
            const std::expected<void, std::string> value{std::unexpected<std::string>{"failed"}};
            const auto                             encoded = encode_expected(value);

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

    TEST_CASE("std::expected codec keeps aggregate payloads as complete items") {
        const std::expected<expected_point, int> original{expected_point{1, 2}};
        const auto                               encoded = encode_expected(original);
        const auto                               decoded = decode_expected<expected_point, int>(encoded);
        REQUIRE(decoded.has_value());
        CHECK_EQ(*decoded, (expected_point{1, 2}));
    }

    TEST_CASE("std::expected codec preserves explicit empty-type customizations") {
        for (bool success : {false, true}) {
            const std::expected<customized_empty, customized_empty> original =
                success ? std::expected<customized_empty, customized_empty>{} : std::unexpected{customized_empty{}};
            const auto encoded = encode_expected(original);
            const auto decoded = decode_expected<customized_empty, customized_empty>(encoded);
            CHECK_EQ(decoded.has_value(), success);
        }
    }

    TEST_CASE("std::expected codec accepts recursive aggregate payload schemas") {
        const std::expected<recursive_expected_payload, int> original{recursive_expected_payload{1, {{2, {}}}}};
        const auto                                           encoded = encode_expected(original);
        const auto                                           decoded = decode_expected<recursive_expected_payload, int>(encoded);
        REQUIRE(decoded.has_value());
        CHECK_EQ(*decoded, *original);
    }

    TEST_CASE("std::expected codec accepts statically empty array payloads") {
        SUBCASE("zero-extent array") {
            using array_type   = std::array<empty_expected_element, 0>;
            const auto encoded = encode_expected(std::expected<array_type, int>{});
            const auto decoded = decode_expected<array_type, int>(encoded);
            REQUIRE(decoded.has_value());
            CHECK(decoded->empty());
        }
        SUBCASE("zero maximum size") {
            using array_type   = bounded_size<std::vector<empty_expected_element>, 0, 0>;
            const auto encoded = encode_expected(std::expected<array_type, int>{});
            const auto decoded = decode_expected<array_type, int>(encoded);
            REQUIRE(decoded.has_value());
            CHECK(decoded->value().empty());
        }
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

    TEST_CASE("std::expected codec preserves installed empty-item mixins") {
        for (bool success : {false, true}) {
            const std::expected<mixin_empty, mixin_empty> original =
                success ? std::expected<mixin_empty, mixin_empty>{} : std::unexpected{mixin_empty{}};
            std::vector<std::byte> bytes;
            auto                   enc = make_encoder<empty_item_codec, std_expected_codec>(bytes);
            REQUIRE(enc(original, 7));
            auto                                    dec = make_decoder<empty_item_codec, std_expected_codec>(bytes);
            std::expected<mixin_empty, mixin_empty> decoded;
            REQUIRE(dec(decoded));
            CHECK_EQ(decoded.has_value(), original.has_value());
            int following{};
            REQUIRE(dec(following));
            CHECK_EQ(following, 7);
        }
    }

    TEST_CASE("std::expected codec finds mixins inside containers") {
        const std::expected<std::vector<mixin_empty>, int> original{std::vector<mixin_empty>(2)};
        std::vector<std::byte>                             bytes;
        auto                                               enc = make_encoder<empty_item_codec, std_expected_codec>(bytes);
        REQUIRE(enc(original));
        auto                                         dec = make_decoder<empty_item_codec, std_expected_codec>(bytes);
        std::expected<std::vector<mixin_empty>, int> decoded;
        REQUIRE(dec(decoded));
        REQUIRE(decoded.has_value());
        CHECK_EQ(*decoded, *original);
    }

} // TEST_SUITE

#endif
