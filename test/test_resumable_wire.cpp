#include "test_util.h"

#include <array>
#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

using namespace cbor::tags;

namespace {

struct empty_group {};
struct single_group {
    int value{};
};
struct two_group {
    int first{};
    int second{};
};

status_code validation_status(std::span<const std::byte> input) {
    auto       decoder = make_decoder(input);
    const auto result  = validate_item(decoder);
    return result ? status_code::success : result.error();
}

status_code resumable_status(const expected<std::size_t, status_code> &result) { return result ? status_code::success : result.error(); }

} // namespace

TEST_SUITE("cbor_wire/resumable") {

    TEST_CASE("validation matches one-shot status at every split and eof") {
        for (const auto *hex : {"01", "1a01020304", "826268694200ff", "9fa101d864826268694200ffff", "7f62c3a9ff", "7f61c361a9ff", "f800",
                                "ff", "bf01ff", "5f6161ff", "1c", "5a7fffffff"}) {
            CAPTURE(hex);
            const auto                       wire = to_bytes(hex);
            const std::span<const std::byte> bytes{wire};
            for (std::size_t cut = 0; cut <= bytes.size(); ++cut) {
                CAPTURE(cut);
                const auto prefix    = bytes.first(cut);
                auto       truncated = make_resumable_validator();
                (void)truncated(prefix, end_of_input);
                REQUIRE(truncated.done());
                CHECK_EQ(resumable_status(truncated.result()), validation_status(prefix));

                auto split = make_resumable_validator();
                (void)split(prefix);
                if (!split.done())
                    (void)split(bytes.subspan(cut), end_of_input);
                REQUIRE(split.done());
                CHECK_EQ(resumable_status(split.result()), validation_status(bytes));
            }
        }
    }

    TEST_CASE("group arities and tags retain synchronous wire shapes") {
        empty_group               empty;
        single_group              single{7};
        two_group                 pair{1, 2};
        auto                      operation = make_resumable_encoder()(empty, single, pair, static_tag<100>{}, 9);
        std::array<std::byte, 16> output{};
        const auto                progress = operation(output);
        REQUIRE(operation.result());
        CHECK_EQ(progress.bytes, 7U);
        std::vector<std::byte> actual;
        for (const auto prefix : progress.produced())
            for (const auto byte : prefix)
                actual.push_back(byte);
        CHECK_EQ(to_hex(actual), "07820102d86409");

        empty_group  decoded_empty;
        single_group decoded_single{};
        two_group    decoded_pair{};
        int          tagged{};
        auto         decode = make_resumable_decoder()(decoded_empty, decoded_single, decoded_pair, static_tag<100>{}, tagged);
        (void)decode(actual);
        REQUIRE(decode.result());
        CHECK_EQ(decoded_single.value, 7);
        CHECK_EQ(decoded_pair.first, 1);
        CHECK_EQ(decoded_pair.second, 2);
        CHECK_EQ(tagged, 9);
    }

    TEST_CASE("terminal results preserve the ordered unread suffix") {
        const auto wire      = to_bytes("1801090a0b");
        auto       value     = std::uint64_t{};
        auto       operation = make_resumable_decoder()(value);
        std::array chunks{std::span{wire}.first(3), std::span{wire}.subspan(3)};
        const auto progress = operation(chunks);
        REQUIRE(operation.result());
        CHECK_EQ(value, 1U);
        CHECK_EQ(progress.bytes, 2U);
        std::vector<std::byte> remaining;
        for (const auto &chunk : operation.remaining_input())
            for (const auto byte : chunk)
                remaining.push_back(byte);
        CHECK_EQ(to_hex(remaining), "090a0b");
    }

    TEST_CASE("validation counts items and leaves trailing bytes untouched") {
        const auto wire      = to_bytes("010203");
        auto       validator = make_resumable_validator(2);
        (void)validator(wire);
        REQUIRE(validator.result());
        CHECK_EQ(*validator.result(), 2U);
        std::vector<std::byte> remaining;
        for (const auto &chunk : validator.remaining_input())
            for (const auto byte : chunk)
                remaining.push_back(byte);
        CHECK_EQ(to_hex(remaining), "03");
    }
}
