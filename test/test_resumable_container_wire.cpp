#include "test_util.h"

#include <cbor_tags/cbor_resumable.h>
#include <cstdint>
#include <limits>
#include <list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace cbor::tags;

namespace {

using input_chunks = std::list<std::span<const std::byte>>;

template <typename T> void check_integer_array_splits(std::string_view hex, const std::vector<T> &expected) {
    const auto                       bytes = to_bytes(hex);
    const std::span<const std::byte> input{bytes};
    const auto                       item_size = bytes.size() - 1;
    for (std::size_t cut = 0; cut < item_size; ++cut) {
        CAPTURE(hex);
        CAPTURE(cut);
        input_chunks   storage;
        auto           source = make_input_source(storage);
        std::vector<T> actual;
        auto           operation = make_resumable_decoder(source)(actual);
        operation(std::span<const std::byte>{}, input.first(cut));
        REQUIRE_FALSE(operation.done());
        operation(std::span<const std::byte>{}, input.subspan(cut));

        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(*operation.result() == item_size);
        CHECK(actual == expected);
        CHECK(to_hex(operation.remaining_input()) == "09");
    }
}

} // namespace

TEST_SUITE("cbor_wire/resumable") {

    TEST_CASE("definite and indefinite integer arrays retain widths at every split") {
        const std::vector<std::uint64_t> positive{0, 24, 256, 65536, 0x0102030405060708ULL};
        check_integer_array_splits("850018181901001a000100001b010203040506070809", positive);
        check_integer_array_splits("9f0018181901001a000100001b0102030405060708ff09", positive);

        const std::vector<std::int64_t> negative_values{-1, -25, -257, -65537, std::numeric_limits<std::int64_t>::min()};
        check_integer_array_splits("852038183901003a000100003b7fffffffffffffff09", negative_values);
        check_integer_array_splits("9f2038183901003a000100003b7fffffffffffffffff09", negative_values);
    }

    TEST_CASE("indefinite integers preserve wide wire narrowing and strict overflow") {
        check_integer_array_splits<std::uint32_t>("9f1b00000000000001001b0000000100000007ff09", {256, 7});
        const auto                       bytes = to_bytes("9f1b00000000000001001b0000000100000007ff09");
        const std::span<const std::byte> input{bytes};
        for (std::size_t cut = 0; cut <= input.size(); ++cut) {
            CAPTURE(cut);
            input_chunks               storage;
            auto                       source = make_input_source(storage);
            std::vector<std::uint32_t> actual;
            auto                       operation = make_resumable_decoder_with_options<strict_integer_decoder_options>(source)(actual);
            operation(input.first(cut));
            if (!operation.done())
                operation(input.subspan(cut), end_of_input);

            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::no_match_for_uint_on_buffer);
            CHECK(actual == std::vector<std::uint32_t>{256});
        }
    }

    TEST_CASE("indefinite integer bounds count elements and accept a break at the maximum") {
        const auto bytes = to_bytes("9f011818ff09");
        auto       check = [&]<std::size_t Minimum, std::size_t Maximum>(status_code expected_status, std::vector<int> expected,
                                                                         std::string_view tail) {
            input_chunks     storage;
            auto             source = make_input_source(storage);
            std::vector<int> actual;
            auto             indefinite = as_indefinite{actual};
            auto             bounded    = as_bounded_size<Minimum, Maximum>(indefinite);
            auto             operation  = make_resumable_decoder(source)(bounded);
            operation(bytes);
            REQUIRE(operation.done());
            const auto result = operation.result();
            CHECK((result ? status_code::success : result.error()) == expected_status);
            CHECK(actual == expected);
            CHECK(to_hex(operation.remaining_input()) == tail);
            if (result)
                CHECK(*result == 5U);
        };
        check.template operator()<2, 2>(status_code::success, {1, 24}, "09");
        check.template operator()<3, 3>(status_code::size_limit_exceeded, {1, 24}, "09");
        check.template operator()<0, 1>(status_code::size_limit_exceeded, {1}, "1818ff09");

        const auto       empty = to_bytes("9fff09");
        input_chunks     storage;
        auto             source = make_input_source(storage);
        std::vector<int> actual;
        auto             bounded   = as_bounded_size<0, 0>(actual);
        auto             operation = make_resumable_decoder(source)(bounded);
        operation(empty);
        REQUIRE(operation.result());
        CHECK(*operation.result() == 2U);
        CHECK(actual.empty());
        CHECK(to_hex(operation.remaining_input()) == "09");
    }

    TEST_CASE("indefinite integer eof retains completed elements and an incomplete item") {
        auto check = [](std::string_view hex, std::vector<int> expected, std::string_view tail) {
            const auto       bytes = to_bytes(hex);
            input_chunks     storage;
            auto             source = make_input_source(storage);
            std::vector<int> actual;
            auto             operation = make_resumable_decoder(source)(actual);
            operation(bytes);
            REQUIRE_FALSE(operation.done());
            CHECK(actual == expected);
            operation.finish();
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::incomplete);
            CHECK(actual == expected);
            CHECK(to_hex(operation.remaining_input()) == tail);
        };
        check("9f01190100", {1, 256}, "");
        check("9f011901", {1}, "1901");
    }

    TEST_CASE("indefinite integer errors retain the offending item after a committed prefix") {
        const std::pair<std::string_view, status_code> cases[]{
            {"1c", status_code::invalid_additional_info},
            {"20", status_code::no_match_for_uint_on_buffer},
            {"1b0000000100000000", status_code::no_match_for_uint_on_buffer},
        };
        for (const auto &[item, expected_status] : cases) {
            CAPTURE(item);
            const auto                 tail  = std::string{item} + "ff090000000000000000";
            const auto                 bytes = to_bytes("9f01" + tail);
            input_chunks               storage;
            auto                       source = make_input_source(storage);
            std::vector<std::uint32_t> actual;
            auto                       operation = make_resumable_decoder_with_options<strict_integer_decoder_options>(source)(actual);
            operation(bytes);
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == expected_status);
            CHECK(actual == std::vector<std::uint32_t>{1});
            CHECK(to_hex(operation.remaining_input()) == tail);
        }
    }
}
