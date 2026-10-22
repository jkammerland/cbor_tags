#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/codec.h>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using namespace cbor::tags;

namespace {

using count_options = Options<Option<expected<std::size_t, status_code>>, default_wrapping>;

struct scalar_customization {
    unsigned number{1};

    template <typename Encoder> auto encode(Encoder &enc) const { return enc(number); }
    template <typename Decoder> auto decode(Decoder &dec) { return dec(number); }
};

struct integral_tagged : scalar_customization {
    static constexpr std::uint64_t cbor_tag = 100;
};

enum protocol_tag : std::uint64_t { sample_tag = 100 };

struct enum_tagged : scalar_customization {
    static constexpr protocol_tag cbor_tag = sample_tag;
};

struct convertible_tag {
    constexpr operator std::uint64_t() const { return 100; }
};

struct convertible_tagged : scalar_customization {
    static constexpr convertible_tag cbor_tag{};
};

struct mutable_numeric_tag {
    constexpr operator std::uint64_t() { return 100; }
};

struct mutable_conversion_tagged : scalar_customization {
    mutable_numeric_tag cbor_tag;
};

struct rvalue_numeric_tag {
    constexpr operator std::uint64_t() && { return 100; }
};

struct rvalue_conversion_tagged : scalar_customization {
    rvalue_numeric_tag cbor_tag;
};

struct member_static_tagged : scalar_customization {
    static_tag<100> cbor_tag;
};

struct member_dynamic_tagged : scalar_customization {
    dynamic_tag<std::uint64_t> cbor_tag{100};
};

struct adl_integral_tagged : scalar_customization {};
constexpr std::uint64_t cbor_tag(const adl_integral_tagged &) { return 100; }

struct adl_static_tagged : scalar_customization {};
constexpr static_tag<100> cbor_tag(const adl_static_tagged &) { return {}; }

struct adl_dynamic_tagged : scalar_customization {};
constexpr dynamic_tag<std::uint64_t> cbor_tag(const adl_dynamic_tagged &) { return {100}; }

struct function_tagged {
    unsigned number{1};

    static constexpr std::uint64_t cbor_tag() { return 100; }

    // Function metadata keeps the established explicitly tagged customization.
    template <typename Encoder> auto encode(Encoder &enc) const { return enc(static_tag<cbor_tag()>{}, number); }
    template <typename Decoder> auto decode(Decoder &dec) { return dec(static_tag<cbor_tag()>{}, number); }
};

template <typename Self> struct rvalue_tag_codec : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;

    unsigned rvalue_tag_encodes{};
    unsigned rvalue_tag_decodes{};

    void encode(static_tag<100> &&) {
        ++rvalue_tag_encodes;
        static_cast<Self &>(*this).encode_tag_argument(100);
    }

    void encode(dynamic_tag<std::uint64_t> &&tag) {
        ++rvalue_tag_encodes;
        static_cast<Self &>(*this).encode_tag_argument(tag.cbor_tag);
    }

    status_code decode(static_tag<100> &&tag) {
        ++rvalue_tag_decodes;
        auto &dec                = static_cast<Self &>(*this);
        const auto [major, info] = dec.read_initial_byte();
        return dec.decode(tag, major, info);
    }

    status_code decode(dynamic_tag<std::uint64_t> &&tag) {
        ++rvalue_tag_decodes;
        auto &dec                = static_cast<Self &>(*this);
        const auto [major, info] = dec.read_initial_byte();
        return dec.decode(tag, major, info);
    }
};

template <typename Tagged, typename Options> void check_qualified_numeric_metadata() {
    std::vector<std::byte> output;
    const auto             encoded = make_encoder_with_options<Options>(output)(Tagged{});
    REQUIRE(encoded);
    CHECK_EQ(to_hex(output), "d86401");

    const auto input = to_bytes("d86401");
    Tagged     destination;
    destination.number = 77;
    const auto decoded = make_decoder_with_options<Options>(input)(destination);
    REQUIRE(decoded);
    CHECK_EQ(destination.number, 1U);

    for (const auto &test :
         {std::pair{"186401", status_code::no_match_for_tag_on_buffer}, std::pair{"d86501", status_code::no_match_for_tag}}) {
        CAPTURE(test.first);
        const auto invalid  = to_bytes(test.first);
        destination.number  = 77;
        const auto rejected = make_decoder_with_options<Options>(invalid)(destination);
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error() == test.second);
        CHECK_EQ(destination.number, 77U);
    }

    if constexpr (detail::returns_byte_count_v<typename Options::return_type>) {
        CHECK_EQ(*encoded, 3U);
        CHECK_EQ(*decoded, 3U);
    }
}

static_assert(IsClassWithTagOverload<integral_tagged>);
static_assert(IsClassWithTagOverload<mutable_conversion_tagged>);
static_assert(IsClassWithTagOverload<rvalue_conversion_tagged>);
static_assert(!IsClassWithTagOverload<function_tagged>);

} // namespace

TEST_SUITE("cbor_wire/custom_class_tags") {

    TEST_CASE_TEMPLATE("numeric metadata preserves admitted conversion qualifiers", Tagged, mutable_conversion_tagged,
                       rvalue_conversion_tagged) {
        SUBCASE("default results") { check_qualified_numeric_metadata<Tagged, default_options>(); }
        SUBCASE("byte count results") { check_qualified_numeric_metadata<Tagged, count_options>(); }
    }

    TEST_CASE_TEMPLATE("automatic wrapper tags retain rvalue codec dispatch", Tagged, member_static_tagged, member_dynamic_tagged) {
        std::vector<std::byte> output;
        auto                   enc     = make_encoder_with_options<count_options, rvalue_tag_codec>(output);
        const auto             encoded = enc(Tagged{});
        REQUIRE(encoded);
        CHECK_EQ(*encoded, 3U);
        CHECK_EQ(to_hex(output), "d86401");
        CHECK_EQ(enc.rvalue_tag_encodes, 1U);

        const auto input = to_bytes("d86401");
        auto       dec   = make_decoder_with_options<count_options, rvalue_tag_codec>(input);
        Tagged     destination;
        destination.number = 77;
        const auto decoded = dec(destination);
        REQUIRE(decoded);
        CHECK_EQ(*decoded, 3U);
        CHECK_EQ(destination.number, 1U);
        CHECK_EQ(dec.rvalue_tag_decodes, 1U);
    }

    TEST_CASE_TEMPLATE("custom integral tag metadata encodes and validates a tag item", Options, default_options, count_options) {
        std::vector<std::byte> output;
        integral_tagged        source;
        const auto             encoded = make_encoder_with_options<Options>(output)(source);
        REQUIRE(encoded);
        CHECK_EQ(to_hex(output), "d86401");

        const auto      input = to_bytes("d86401");
        integral_tagged destination;
        destination.number = 77;
        auto       dec     = make_decoder_with_options<Options>(input);
        const auto decoded = dec(destination);
        REQUIRE(decoded);
        CHECK_EQ(destination.number, 1U);
        CHECK(dec.tell() == input.cend());

        if constexpr (detail::returns_byte_count_v<typename Options::return_type>) {
            CHECK_EQ(*encoded, 3U);
            CHECK_EQ(*decoded, 3U);
        }
    }

    TEST_CASE_TEMPLATE("custom integral tags work in variants and nested containers", Options, default_options, count_options) {
        integral_tagged source;
        source.number = 24;
        std::variant<unsigned, integral_tagged> source_variant{source};
        std::vector<std::byte>                  output;
        const auto                              encoded = make_encoder_with_options<Options>(output)(source_variant);
        REQUIRE(encoded);
        CHECK_EQ(to_hex(output), "d8641818");

        const auto                              input = to_bytes("d8641818");
        std::variant<unsigned, integral_tagged> destination{7U};
        const auto                              decoded = make_decoder_with_options<Options>(input)(destination);
        REQUIRE(decoded);
        REQUIRE(std::holds_alternative<integral_tagged>(destination));
        CHECK_EQ(std::get<integral_tagged>(destination).number, 24U);

        // An indefinite container redispatches a consumed initial byte to each customization.
        const auto                   nested_input = to_bytes("9fd86401d8641818ff");
        std::vector<integral_tagged> nested;
        const auto                   nested_decoded = make_decoder_with_options<Options>(nested_input)(nested);
        REQUIRE(nested_decoded);
        REQUIRE_EQ(nested.size(), 2U);
        CHECK_EQ(nested[0].number, 1U);
        CHECK_EQ(nested[1].number, 24U);

        if constexpr (detail::returns_byte_count_v<typename Options::return_type>) {
            CHECK_EQ(*encoded, 4U);
            CHECK_EQ(*decoded, 4U);
            CHECK_EQ(*nested_decoded, 9U);
        }
    }

    TEST_CASE_TEMPLATE("custom enum tag metadata encodes and validates its tag number", Options, default_options, count_options) {
        std::vector<std::byte> output;
        const auto             encoded = make_encoder_with_options<Options>(output)(enum_tagged{});
        REQUIRE(encoded);
        CHECK_EQ(to_hex(output), "d86401");

        const auto  input = to_bytes("d86401");
        enum_tagged destination;
        destination.number = 77;
        const auto decoded = make_decoder_with_options<Options>(input)(destination);
        REQUIRE(decoded);
        CHECK_EQ(destination.number, 1U);

        const auto wrong_tag = to_bytes("d86501");
        destination.number   = 77;
        const auto rejected  = make_decoder_with_options<Options>(wrong_tag)(destination);
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error() == status_code::no_match_for_tag);
        CHECK_EQ(destination.number, 77U);

        if constexpr (detail::returns_byte_count_v<typename Options::return_type>) {
            CHECK_EQ(*encoded, 3U);
            CHECK_EQ(*decoded, 3U);
        }
    }

    TEST_CASE_TEMPLATE("custom integral tags reject wrong or malformed tag headers", Options, default_options, count_options) {
        struct failure {
            const char *wire;
            status_code expected_status;
            std::size_t consumed;
        };

        for (const auto &test : {
                 failure{"186401", status_code::no_match_for_tag_on_buffer, 1},
                 failure{"d86501", status_code::no_match_for_tag, 2},
                 failure{"d8", status_code::incomplete, 1},
                 failure{"dc01", status_code::invalid_additional_info, 1},
                 failure{"d864", status_code::incomplete, 2},
                 failure{"d864f5", status_code::no_match_for_uint_on_buffer, 3},
             }) {
            CAPTURE(test.wire);
            const auto      input = to_bytes(test.wire);
            integral_tagged destination;
            destination.number = 77;
            auto       dec     = make_decoder_with_options<Options>(input);
            const auto result  = dec(destination);
            REQUIRE_FALSE(result);
            CHECK(result.error() == test.expected_status);
            CHECK_EQ(destination.number, 77U);
            CHECK(dec.tell() == input.cbegin() + static_cast<std::ptrdiff_t>(test.consumed));
        }
    }

    TEST_CASE_TEMPLATE("automatic member and adl tag forms preserve tag item dispatch", Tagged, member_static_tagged, member_dynamic_tagged,
                       adl_integral_tagged, adl_static_tagged, adl_dynamic_tagged, convertible_tagged) {
        std::vector<std::byte> output;
        Tagged                 source;
        REQUIRE(make_encoder(output)(source));
        CHECK_EQ(to_hex(output), "d86401");

        const auto input = to_bytes("d86401");
        Tagged     destination;
        destination.number = 77;
        REQUIRE(make_decoder(input)(destination));
        CHECK_EQ(destination.number, 1U);
    }

    TEST_CASE("runtime member tags validate the caller supplied expected tag") {
        member_dynamic_tagged source;
        source.cbor_tag.cbor_tag = 200;
        std::vector<std::byte> output;
        REQUIRE(make_encoder(output)(source));
        CHECK_EQ(to_hex(output), "d8c801");

        const auto            input = to_bytes("d8c801");
        member_dynamic_tagged destination;
        destination.cbor_tag.cbor_tag = 200;
        destination.number            = 77;
        REQUIRE(make_decoder(input)(destination));
        CHECK_EQ(destination.number, 1U);
        CHECK_EQ(destination.cbor_tag.cbor_tag, 200U);

        member_dynamic_tagged mismatch;
        mismatch.number   = 77;
        const auto result = make_decoder(input)(mismatch);
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::no_match_for_tag);
        CHECK_EQ(mismatch.number, 77U);
        CHECK_EQ(mismatch.cbor_tag.cbor_tag, 100U);
    }

    TEST_CASE_TEMPLATE("tag functions retain explicitly tagged customization behavior", Options, default_options, count_options) {
        std::vector<std::byte> output;
        const auto             encoded = make_encoder_with_options<Options>(output)(function_tagged{});
        REQUIRE(encoded);
        CHECK_EQ(to_hex(output), "d86401");

        const auto      input = to_bytes("d86401");
        function_tagged destination;
        destination.number = 77;
        const auto decoded = make_decoder_with_options<Options>(input)(destination);
        REQUIRE(decoded);
        CHECK_EQ(destination.number, 1U);

        if constexpr (detail::returns_byte_count_v<typename Options::return_type>) {
            CHECK_EQ(*encoded, 3U);
            CHECK_EQ(*decoded, 3U);
        }
    }
}
