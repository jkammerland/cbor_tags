#include "test_util.h"

#include <array>
#include <cbor_tags/cbor_segments.h>
#include <cbor_tags/cbor_traversal.h>
#include <span>
#include <string_view>

using namespace cbor::tags;

TEST_SUITE("cbor_wire/segments") {
    TEST_CASE("segment structure checks remain distinct from strict item validation") {
        struct fixture {
            std::string_view hex;
            status_code      strict_status;
        };
        constexpr std::array cases{
            fixture{"61ff", status_code::invalid_utf8_sequence},
            fixture{"f800", status_code::malformed_structure},
            fixture{"6161", status_code::success},
            fixture{"f820", status_code::success},
        };

        for (const auto &test : cases) {
            CAPTURE(test.hex);
            const auto input = to_bytes(test.hex);
            const auto bytes = std::span<const std::byte>{input};

            // Transport splits do not change the permissive structural contract.
            for (std::size_t split = 0; split <= bytes.size(); ++split) {
                CAPTURE(split);
                byte_segments segments;
                segments.append_owned_segment(bytes.first(split));
                segments.append_owned_segment(bytes.subspan(split));

                // Keep two distinct descriptors, including empty boundary segments.
                REQUIRE(segments.size() == 2);
                REQUIRE(segments.front().size() == split);
                REQUIRE(segments.back().size() == bytes.size() - split);

                const auto item = check_item_segments_structure(std::move(segments));
                REQUIRE(item);
                CHECK(item->flatten() == input);
            }

            // The strict API additionally checks text and simple-value encodings.
            auto       decoder = make_decoder(input);
            const auto result  = validate_item(decoder);
            if (test.strict_status == status_code::success) {
                REQUIRE(result);
            } else {
                REQUIRE_FALSE(result);
                CHECK(result.error() == test.strict_status);
            }
        }
    }
}
