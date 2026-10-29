#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <doctest/doctest.h>
#include <list>
#include <ranges>

using namespace cbor::tags;

TEST_SUITE("cbor_wire/encoded_item_views") {
    TEST_CASE("consumed header bytes do not claim a complete item for forwarding") {
        const auto input   = to_bytes("c101");
        auto       decoder = make_decoder_with_options<consumed_bytes_decoder_options>(input);
        const auto prefix  = decoder(static_tag<1>{});

        REQUIRE(prefix);
        static_assert(std::same_as<typename decltype(prefix)::value_type, encoded_byte_view_for<decltype(input)>>);
        static_assert(!IsEncodedItemView<typename decltype(prefix)::value_type>);
        CHECK(to_hex(*prefix) == "c1");
        CHECK(prefix->span().data() == input.data());

        // A byte range encodes as a byte string, not an unchecked complete item.
        std::vector<std::byte> output;
        REQUIRE(make_encoder(output)(*prefix));
        CHECK(to_hex(output) == "41c1");

        int        payload{};
        const auto remaining = decoder(payload);
        REQUIRE(remaining);
        CHECK(payload == 1);
        CHECK(to_hex(*remaining) == "01");
    }

    TEST_CASE("consumed byte returns preserve precise header errors and terminal cursors") {
        auto        bytes           = to_bytes("a0");
        status_code expected_status = status_code::no_match_for_array_on_buffer;
        SUBCASE("wrong major type") {}
        SUBCASE("reserved additional information") {
            bytes           = to_bytes("9c");
            expected_status = status_code::invalid_additional_info;
        }
        SUBCASE("truncated argument") {
            bytes           = to_bytes("99ff");
            expected_status = status_code::incomplete;
        }

        auto dec    = make_decoder_with_options<consumed_bytes_decoder_options>(bytes);
        auto result = dec(as_array{0});
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), expected_status);
        // Contiguous fixed-width arguments reject missing bytes before consuming
        // the payload; unsized input consumes the available payload once.
        CHECK(dec.tell() == bytes.begin() + 1);

        const std::list<std::byte> storage(bytes.begin(), bytes.end());
        const std::ranges::subrange<std::list<std::byte>::const_iterator, std::list<std::byte>::const_iterator,
                                    std::ranges::subrange_kind::unsized>
            input{storage.begin(), storage.end()};
        static_assert(!std::ranges::sized_range<decltype(input)>);
        auto unsized_dec    = make_decoder_with_options<consumed_bytes_decoder_options>(input);
        auto unsized_result = unsized_dec(as_array{0});
        REQUIRE_FALSE(unsized_result);
        CHECK_EQ(unsized_result.error(), expected_status);
        CHECK(unsized_dec.tell() == input.end());
    }
}
