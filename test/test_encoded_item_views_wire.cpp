#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <doctest/doctest.h>
#include <list>
#include <ranges>

using namespace cbor::tags;

TEST_SUITE("cbor_wire/encoded_item_views") {
    TEST_CASE("encoded item views preserve precise header errors and terminal cursors") {
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

        auto dec    = make_decoder_with_options<encoded_item_view_decoder_options>(bytes);
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
        auto unsized_dec    = make_decoder_with_options<encoded_item_view_decoder_options>(input);
        auto unsized_result = unsized_dec(as_array{0});
        REQUIRE_FALSE(unsized_result);
        CHECK_EQ(unsized_result.error(), expected_status);
        CHECK(unsized_dec.tell() == input.end());
    }
}
