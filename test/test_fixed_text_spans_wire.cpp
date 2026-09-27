#include "test_util.h"

#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cstddef>
#include <deque>
#include <doctest/doctest.h>
#include <span>
#include <string_view>
#include <utility>

using namespace cbor::tags;

TEST_SUITE("cbor_wire/fixed_text_spans") {

    TEST_CASE("fixed-extent text spans decode exact payloads without copying") {
        const auto                input = to_bytes("6361626307");
        const std::array<char, 3> initial{'x', 'y', 'z'};
        std::span<const char, 3>  output{initial};
        auto                      dec = make_decoder(input);
        REQUIRE(dec(output));
        CHECK_EQ(std::string_view(output.data(), output.size()), "abc");
        CHECK_EQ(static_cast<const void *>(output.data()), static_cast<const void *>(input.data() + 1));
        int following{};
        REQUIRE(dec(following));
        CHECK_EQ(following, 7);
    }

    TEST_CASE("fixed-extent text spans reject length mismatches and incomplete payloads") {
        for (const auto &[hex, expected_status] : {
                 std::pair{"626162", status_code::unexpected_group_size},
                 std::pair{"6461626364", status_code::unexpected_group_size},
                 std::pair{"636162", status_code::incomplete},
             }) {
            CAPTURE(hex);
            const auto                input = to_bytes(hex);
            const std::array<char, 3> initial{'x', 'y', 'z'};
            std::span<const char, 3>  output{initial};
            const auto                result = make_decoder(input)(output);
            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), expected_status);
            CHECK_EQ(output.data(), initial.data());
        }
    }

    TEST_CASE("zero-extent text spans decode empty text") {
        const auto               input = to_bytes("6007");
        std::span<const char, 0> output;
        auto                     dec = make_decoder(input);
        REQUIRE(dec(output));
        CHECK(output.empty());
        int following{};
        REQUIRE(dec(following));
        CHECK_EQ(following, 7);
    }

    TEST_CASE("bounded fixed-extent text spans retain their extent check") {
        const auto                input = to_bytes("626162");
        const std::array<char, 3> initial{'x', 'y', 'z'};
        std::span<const char, 3>  output{initial};
        const auto                result = make_decoder(input)(as_bounded_size(output, 0, 4));
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::unexpected_group_size);
        CHECK_EQ(output.data(), initial.data());
    }

    TEST_CASE("fixed-extent text spans reject non-contiguous input") {
        const auto                  bytes = to_bytes("63616263");
        const std::deque<std::byte> input(bytes.begin(), bytes.end());
        const std::array<char, 3>   initial{'x', 'y', 'z'};
        std::span<const char, 3>    output{initial};
        const auto                  result = make_decoder(input)(output);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::contiguous_view_on_non_contiguous_data);
        CHECK_EQ(output.data(), initial.data());
    }

} // TEST_SUITE
