#if __has_include(<version>)
#include <version>
#endif

#if __has_include(<expected>) && defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
#include "std_expected_test_types.h"
#include "test_util.h"

#include <array>
#include <optional>
#include <span>

using namespace cbor::tags;
using namespace std_expected_test;

namespace {
template <typename T, typename E> void check_decode_error(const char *hex, status_code expected_status) {
    const auto          bytes = to_bytes(hex);
    std::expected<T, E> decoded{};
    auto                dec    = make_decoder<codec::std_expected>(bytes);
    const auto          result = dec(decoded);

    CAPTURE(hex);
    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), expected_status);
}

} // namespace

TEST_SUITE("cbor_wire/std_expected") {

    TEST_CASE("std::expected codec emits the selected alternative and payload") {
        CHECK_EQ(to_hex(encode_expected(std::expected<std::uint64_t, std::string>{42U})), "82f5182a");
        CHECK_EQ(to_hex(encode_expected(std::expected<std::uint64_t, std::string>{std::unexpected<std::string>{"bad"}})), "82f463626164");
        CHECK_EQ(to_hex(encode_expected(std::expected<void, std::string>{})), "82f5f6");
        CHECK_EQ(to_hex(encode_expected(std::expected<void, std::string>{std::unexpected<std::string>{"failed"}})), "82f4666661696c6564");
    }

    TEST_CASE("std::expected aggregate payload is a complete wrapped item") {
        const auto encoded = encode_expected(std::expected<expected_point, int>{expected_point{1, 2}});
        CHECK_EQ(to_hex(encoded), "82f5820102");
        encoded_item_view item;
        REQUIRE(make_decoder(encoded)(item));
    }

    TEST_CASE("std::expected customized empty payload is one null item") {
        for (bool success : {false, true}) {
            const std::expected<customized_empty, customized_empty> original =
                success ? std::expected<customized_empty, customized_empty>{} : std::unexpected{customized_empty{}};
            const auto encoded = encode_expected(original);
            CHECK_EQ(to_hex(encoded), success ? "82f5f6" : "82f4f6");
            encoded_item_view item;
            REQUIRE(make_decoder(encoded)(item));
        }
    }

    TEST_CASE("std::expected recursive aggregates preserve group wrappers") {
        const std::expected<recursive_expected_payload, int> original{recursive_expected_payload{1, {{2, {}}}}};
        CHECK_EQ(to_hex(encode_expected(original)), "82f5820181820280");
    }

    TEST_CASE("std::expected statically empty arrays emit a complete empty array") {
        SUBCASE("zero-extent array") {
            CHECK_EQ(to_hex(encode_expected(std::expected<std::array<empty_expected_element, 0>, int>{})), "82f580");
        }
        SUBCASE("zero maximum size") {
            using array_type = bounded_size<std::vector<empty_expected_element>, 0, 0>;
            CHECK_EQ(to_hex(encode_expected(std::expected<array_type, int>{})), "82f580");
        }
        SUBCASE("zero-extent mutable span") {
            CHECK_EQ(to_hex(encode_expected(std::expected<std::span<empty_expected_element, 0>, int>{})), "82f580");
        }
        SUBCASE("zero-extent const span") {
            CHECK_EQ(to_hex(encode_expected(std::expected<std::span<const empty_expected_element, 0>, int>{})), "82f580");
        }
    }

    TEST_CASE("std::expected installed empty-item mixin emits one null payload") {
        std::vector<std::byte> bytes;
        auto                   enc = make_encoder<empty_item_codec, codec::std_expected>(bytes);
        REQUIRE(enc(std::expected<mixin_empty, int>{}));
        CHECK_EQ(to_hex(bytes), "82f5f6");
    }

    TEST_CASE("std::expected codec accepts indefinite two-item arrays") {
        const auto bytes = to_bytes("9ff5182aff");

        std::expected<int, std::string> decoded{};
        auto                            dec = make_decoder<codec::std_expected>(bytes);

        REQUIRE(dec(decoded));
        REQUIRE(decoded.has_value());
        CHECK_EQ(*decoded, 42);
    }

    TEST_CASE("std::expected codec reports malformed wrappers and payloads") {
        check_decode_error<std::uint64_t, std::string>("82f5", status_code::incomplete);
        check_decode_error<std::uint64_t, std::string>("a0", status_code::no_match_for_array_on_buffer);
        check_decode_error<std::uint64_t, std::string>("98", status_code::incomplete);
        check_decode_error<std::uint64_t, std::string>("81f5", status_code::unexpected_group_size);
        check_decode_error<std::uint64_t, std::string>("83f5182a00", status_code::unexpected_group_size);
        check_decode_error<std::uint64_t, std::string>("8200182a", status_code::no_match_for_simple_on_buffer);
        check_decode_error<std::uint64_t, std::string>("82f563626164", status_code::no_match_for_uint_on_buffer);
        check_decode_error<std::uint64_t, std::string>("82f4182a", status_code::no_match_for_tstr_on_buffer);
        check_decode_error<std::uint64_t, std::string>("9ff5182a", status_code::incomplete);
        check_decode_error<std::uint64_t, std::string>("9ff5182a00ff", status_code::unexpected_group_size);
    }

} // TEST_SUITE

#endif
