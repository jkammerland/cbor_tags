#include "test_util.h"

#include <array>
#include <cbor_tags/detail/cbor_utf8.h>
#include <cstddef>
#include <deque>
#include <span>
#include <string_view>
#include <vector>

using namespace cbor::tags;

TEST_SUITE("cbor_wire/traversal") {

    TEST_CASE("utf8 blocks preserve unicode validity across every split and alignment") {
        struct fixture {
            std::string_view hex;
            bool             valid;
        };

        // Unicode boundary code points, forbidden encodings, and incomplete
        // sequences provide an oracle independent of the block implementation.
        constexpr std::array cases{
            fixture{"", true},
            fixture{"007f", true},
            fixture{"c280dfbf", true},
            fixture{"e0a080ed9fbf", true},
            fixture{"ee8080efbfbf", true},
            fixture{"f0908080f48fbfbf", true},
            fixture{"61c3a9e282acf09f98807a", true},
            fixture{"80", false},
            fixture{"c080", false},
            fixture{"c1bf", false},
            fixture{"e08080", false},
            fixture{"eda080", false},
            fixture{"edbfbf", false},
            fixture{"f0808080", false},
            fixture{"f4908080", false},
            fixture{"f5808080", false},
            fixture{"fe", false},
            fixture{"ff", false},
            fixture{"c2", false},
            fixture{"e282", false},
            fixture{"f09f98", false},
            fixture{"c241", false},
            fixture{"e24180", false},
            fixture{"f0904180", false},
        };

        for (const auto &test : cases) {
            CAPTURE(test.hex);
            const auto encoded = to_bytes(test.hex);
            CHECK(detail::is_valid_utf8(encoded) == test.valid);
            for (std::size_t alignment = 0; alignment < 16; ++alignment) {
                CAPTURE(alignment);
                std::vector<std::byte> storage(alignment + 32, std::byte{'a'});
                storage.insert(storage.end(), encoded.begin(), encoded.end());
                storage.insert(storage.end(), 33, std::byte{'z'});
                const auto bytes = std::span<const std::byte>{storage}.subspan(alignment);

                CHECK(detail::is_valid_utf8(bytes) == test.valid);
                const std::deque<std::byte> noncontiguous(bytes.begin(), bytes.end());
                CHECK(detail::is_valid_utf8(noncontiguous) == test.valid);

                for (std::size_t split = 0; split <= bytes.size(); ++split) {
                    CAPTURE(split);
                    detail::utf8_state state;
                    bool               valid = state.consume_span(bytes.first(split));
                    valid                    = state.consume_span(bytes.subspan(split)) && valid;
                    CHECK((valid && state.complete()) == test.valid);
                }
            }
        }
    }

    TEST_CASE("utf8 block validation retains pending code points across empty spans") {
        const auto bytes = to_bytes("6161616161616161f09f98806161616161616161");
        const auto input = std::span<const std::byte>{bytes};

        detail::utf8_state state;
        REQUIRE(state.consume_span(input.first(10)));
        CHECK_FALSE(state.complete());
        CHECK(state.consume_span({}));
        CHECK_FALSE(state.complete());
        REQUIRE(state.consume_span(input.subspan(10)));
        CHECK(state.complete());

        const auto         invalid = to_bytes("e261616161616161616161616161616161");
        detail::utf8_state invalid_state;
        CHECK_FALSE(invalid_state.consume_span(invalid));
        CHECK_FALSE(invalid_state.complete());

        // Empty contiguous storage need not provide subtractable nonnull pointers.
        const std::span<const std::byte> empty;
        CHECK(detail::is_valid_utf8(empty));
    }
}
