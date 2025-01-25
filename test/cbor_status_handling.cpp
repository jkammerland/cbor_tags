#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_integer.h"
#include "magic_enum/magic_enum.hpp"
#include "test_util.h"

#include <cstddef>
#include <doctest/doctest.h>
#include <fmt/base.h>
#include <map>
#include <memory_resource>
#include <type_traits>

using namespace cbor::tags;
using namespace cbor::tags::literals;
using namespace std::string_view_literals;

TEST_SUITE("Decoding the wrong thing") {
    TEST_CASE("Decode wrong tag") {
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        REQUIRE(enc(140_tag, "Hello world!"sv));

        auto        dec = make_decoder(data);
        std::string result;
        auto        result2 = dec(141_tag, result);
        CHECK(!result2);

        { /* Sanity check recovery - TODO: is this what we want? */
            auto result3 = dec(result);
            CHECK(result3);
            CHECK_EQ(result, "Hello world!");
        }
    }

    TEST_CASE_TEMPLATE("Decode wrong major types, from int", T, negative, std::string, std::vector<std::byte>, std::map<int, int>,
                       static_tag<140>, float) {
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        REQUIRE(enc(int{140}));

        auto dec    = make_decoder(data);
        auto result = dec(T{});
        REQUIRE(!result);
        if constexpr (IsNegative<T>) {
            CHECK_EQ(result.error(), status_code::invalid_major_type_for_negative_integer);
        } else if constexpr (IsTextString<T>) {
            CHECK_EQ(result.error(), status_code::invalid_major_type_for_text_string);
        } else if constexpr (IsBinaryString<T>) {
            CHECK_EQ(result.error(), status_code::invalid_major_type_for_binary_string);
        } else if constexpr (IsMap<T>) {
            CHECK_EQ(result.error(), status_code::invalid_major_type_for_map);
        } else if constexpr (IsTag<T>) {
            CHECK_EQ(result.error(), status_code::invalid_major_type_for_tag);
        } else if constexpr (IsSimple<T>) {
            CHECK_EQ(result.error(), status_code::invalid_major_type_for_simple);
        }
    }

    TEST_CASE_TEMPLATE("Decode wrong major types, from tag", T, positive, negative, std::string_view, std::vector<std::byte>,
                       std::map<int, int>, bool, std::nullptr_t, double) {
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        REQUIRE(enc(make_tag_pair(140_tag, 42)));

        auto dec    = make_decoder(data);
        auto result = dec(T{});
        REQUIRE(!result);
        if constexpr (IsUnsigned<T>) {
            CHECK_EQ(result.error(), status_code::invalid_major_type_for_unsigned_integer);
        } else if constexpr (IsNegative<T>) {
            CHECK_EQ(result.error(), status_code::invalid_major_type_for_negative_integer);
        } else if constexpr (IsTextString<T>) {
            CHECK_EQ(result.error(), status_code::invalid_major_type_for_text_string);
        } else if constexpr (IsBinaryString<T>) {
            CHECK_EQ(result.error(), status_code::invalid_major_type_for_binary_string);
        } else if constexpr (IsMap<T>) {
            CHECK_EQ(result.error(), status_code::invalid_major_type_for_map);
        } else if constexpr (IsSimple<T>) {
            CHECK_EQ(result.error(), status_code::invalid_major_type_for_simple);
        }
    }

    TEST_CASE_TEMPLATE("Decode wrong simple tag", T, float, double, bool, std::nullptr_t) {
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        REQUIRE(enc(25_tag, float16_t{3.1f}));

        auto dec    = make_decoder(data);
        auto result = dec(25_tag, T{});
        REQUIRE(!result);
        CHECK_EQ(result.error(), status_code::invalid_tag_for_simple);
    }

    TEST_CASE_TEMPLATE("Decode, too little memory", T, std::pmr::vector<int>) {
        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);
        REQUIRE(enc(T{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));

        // Limit memory for decoding
        std::array<std::byte, 16>           R;
        std::pmr::monotonic_buffer_resource resource(R.data(), R.size(), std::pmr::null_memory_resource());
        T                                   our_decoded_array(&resource);

        auto dec    = make_decoder(data);
        auto result = dec(our_decoded_array);
        REQUIRE(!result);
        CHECK_EQ(result.error(), status_code::out_of_memory);
    }

    TEST_CASE("Decode wrong major type in variant") {
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        REQUIRE(enc(140_tag, "Hello world!"sv));

        auto dec    = make_decoder(data);
        auto result = dec(std::variant<int, float, double, bool, std::nullptr_t>{});
        REQUIRE(!result);
        CHECK_EQ(result.error(), status_code::no_matching_tag_value_in_variant);
    }

    TEST_CASE("Decode wrong major type in variant, with matching types") {
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        REQUIRE(enc(140_tag, "Hello world!"sv));

        {
            auto dec    = make_decoder(data);
            auto result = dec(std::variant<std::pair<static_tag<139>, std::string>, std::pair<static_tag<141>, std::string>>{});
            REQUIRE(!result);
            CHECK_EQ(result.error(), status_code::no_matching_tag_value_in_variant);
        }

        { /* Sanity check with matching tag valu in variant */
            std::variant<std::pair<static_tag<139>, std::string>, std::pair<static_tag<140>, std::string>> variant;
            auto                                                                                           dec     = make_decoder(data);
            auto                                                                                           result2 = dec(variant);
            if (!result2) {
                fmt::print("Error: {}\n", status_message(result2.error()));
            }
            CHECK(result2);
            CHECK(std::holds_alternative<std::pair<static_tag<140>, std::string>>(variant));
        }
    }

    TEST_CASE("Decode dynamic tag, with wrong value") {
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        REQUIRE(enc(dynamic_tag<uint64_t>{140}, "Hello world!"sv));

        auto dec    = make_decoder(data);
        auto result = dec(dynamic_tag<uint64_t>{141}, std::string{});
        REQUIRE(!result);
        CHECK_EQ(result.error(), status_code::invalid_tag_value);
    }
}

TEST_SUITE("Open objects - wrap as etc") {
    TEST_CASE("Basic") {
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        REQUIRE(enc(140_tag, wrap_as_array{1, 2}));

        fmt::print("data: {}\n", to_hex(data));

        auto dec = make_decoder(data);
        int  a, b;
        auto c      = wrap_as_array{a, b};
        auto result = dec(140_tag, c);
        REQUIRE(result);
        CHECK_EQ(a, 1);
        CHECK_EQ(b, 2);
    }
}

// TEST_SUITE("Decode the right thing") {}