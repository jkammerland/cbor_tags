#include "test_util.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cstdint>
#include <deque>
#include <limits>
#include <list>
#include <map>
#include <memory_resource>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace cbor::tags;

namespace {

using input_chunks          = std::list<std::span<const std::byte>>;
using output_chunks         = std::list<std::span<std::byte>>;
using ordered_integer_map   = std::map<std::uint32_t, std::uint64_t>;
using unordered_integer_map = std::unordered_map<std::uint32_t, std::uint64_t>;

struct string_allocation_resource : std::pmr::memory_resource {
    std::size_t allocations{};
    bool        refuse{};

    void *do_allocate(std::size_t bytes, std::size_t alignment) override {
        ++allocations;
        if (refuse)
            throw std::bad_alloc{};
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }
    void do_deallocate(void *pointer, std::size_t bytes, std::size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override { return this == &other; }
};

// Container APIs can expose a wider max_size() than their size_type.
struct narrow_byte_buffer : std::vector<std::byte> {
    using size_type = std::uint32_t;
    size_type requested{};

    size_type   size() const { return static_cast<size_type>(std::vector<std::byte>::size()); }
    size_type   capacity() const { return static_cast<size_type>(std::vector<std::byte>::capacity()); }
    std::size_t max_size() const { return 100; }
    void        reserve(size_type count) {
        requested = count;
        std::vector<std::byte>::reserve(count);
    }
};

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

    TEST_CASE("indefinite string growth accepts mixed size types and clamps to max size") {
        const auto bytes = to_bytes("5f4162ff");
        for (const std::uint32_t initial_capacity : {3U, 60U}) {
            CAPTURE(initial_capacity);
            narrow_byte_buffer actual;
            actual.reserve(initial_capacity);
            actual.assign(actual.capacity(), std::byte{0x61});
            const auto previous_capacity = actual.capacity();
            actual.requested             = 0;
            input_chunks storage;
            auto         source    = make_input_source(storage);
            auto         operation = make_resumable_decoder(source)(actual);
            operation(std::span<const std::byte>{bytes}.first(2));
            REQUIRE_FALSE(operation.done());
            CHECK(actual.size() == previous_capacity);
            CHECK(actual.requested == std::min<std::size_t>(2 * previous_capacity, actual.max_size()));
            const auto capacity = actual.capacity();
            operation(std::span<const std::byte>{bytes}.subspan(2));
            REQUIRE(operation.done());
            REQUIRE(operation.result());
            CHECK(*operation.result() == bytes.size());
            CHECK(actual.size() == previous_capacity + 1);
            CHECK(actual.back() == std::byte{0x62});
            CHECK(actual.capacity() == capacity);
        }
    }

    TEST_CASE_TEMPLATE("indefinite string chunks reserve the declared payload before copying", T, std::pmr::string,
                       std::pmr::vector<std::byte>) {
        for (const bool truncated : {false, true}) {
            CAPTURE(truncated);
            const auto                   header = to_bytes(IsTextString<T> ? "7f7840" : "5f5840");
            const std::vector<std::byte> payload(64, std::byte{0x61});
            const auto                   end = to_bytes("ff09");
            string_allocation_resource   resource;
            T                            actual{&resource};
            actual.assign(8, typename T::value_type{});
            const T prefix{actual};
            resource.allocations = 0;
            input_chunks storage;
            auto         source    = make_input_source(storage);
            auto         operation = make_resumable_decoder(source)(actual);
            operation(header);
            REQUIRE_FALSE(operation.done());
            CHECK(actual == prefix);
            CHECK(actual.capacity() >= prefix.size() + payload.size());
            CHECK(resource.allocations == 1U);
            const auto capacity     = actual.capacity();
            const auto payload_size = truncated ? 3U : payload.size();
            for (const auto &byte : std::span{payload}.first(payload_size)) {
                operation(std::span{&byte, 1});
                CHECK(resource.allocations == 1U);
                CHECK(actual.capacity() == capacity);
            }
            T expected{prefix};
            expected.insert(expected.end(), payload_size, static_cast<typename T::value_type>(0x61));
            CHECK(actual == expected);

            if (truncated) {
                operation.finish();
                REQUIRE(operation.done());
                REQUIRE_FALSE(operation.result());
                CHECK(operation.result().error() == status_code::incomplete);
            } else {
                operation(end);
                REQUIRE(operation.done());
                REQUIRE(operation.result());
                CHECK(*operation.result() == header.size() + payload.size() + 1);
                CHECK(to_hex(operation.remaining_input()) == "09");
            }
            CHECK(actual == expected);
            CHECK(actual.capacity() == capacity);
            CHECK(resource.allocations == 1U);
        }
    }

    TEST_CASE_TEMPLATE("tiny indefinite string chunks grow geometrically without payload reallocations", T, std::pmr::string,
                       std::pmr::vector<std::byte>) {
        constexpr std::size_t count = 512;
        std::string           hex   = IsTextString<T> ? "7f" : "5f";
        for (std::size_t index = 0; index < count; ++index)
            hex += IsTextString<T> ? "6161" : "4161";
        hex += "ff09";
        const auto                       bytes = to_bytes(hex);
        const std::span<const std::byte> input{bytes};
        string_allocation_resource       resource;
        T                                actual{&resource};
        resource.allocations = 0;
        input_chunks storage;
        auto         source    = make_input_source(storage);
        auto         operation = make_resumable_decoder(source)(actual);
        operation(input.first(1));

        for (std::size_t index = 0; index < count; ++index) {
            CAPTURE(index);
            const auto capacity    = actual.capacity();
            const auto allocations = resource.allocations;
            operation(input.subspan(1 + 2 * index, 1));
            REQUIRE_FALSE(operation.done());
            CHECK(actual.size() == index);
            CHECK(actual.capacity() >= index + 1);
            if (capacity >= index + 1) {
                CHECK(resource.allocations == allocations);
            } else {
                CHECK(resource.allocations == allocations + 1);
                CHECK(actual.capacity() >= 2 * capacity);
            }
            const auto reserved_allocations = resource.allocations;
            operation(input.subspan(2 + 2 * index, 1));
            CHECK(resource.allocations == reserved_allocations);
        }
        operation(input.subspan(1 + 2 * count));
        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(*operation.result() == 2 * count + 2);
        CHECK(actual == T(count, static_cast<typename T::value_type>(0x61)));
        CHECK(resource.allocations <= std::bit_width(count) + 1);
        CHECK(to_hex(operation.remaining_input()) == "09");
    }

    TEST_CASE_TEMPLATE("indefinite string child validation precedes reservation", T, std::pmr::string, std::pmr::vector<std::byte>) {
        const std::string          outer = IsTextString<T> ? "7f" : "5f";
        const std::string          major = IsTextString<T> ? "7" : "5";
        string_allocation_resource resource;
        T                          actual{&resource};
        resource.allocations = 0;
        input_chunks storage;
        auto         source = make_input_source(storage);

        SUBCASE("incomplete child header") {
            const auto bytes     = to_bytes(outer + major + "8");
            auto       operation = make_resumable_decoder(source)(actual);
            operation(bytes, end_of_input);
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::incomplete);
            CHECK(resource.allocations == 0U);
        }
        SUBCASE("wrong child major") {
            const auto bytes     = to_bytes(outer + (IsTextString<T> ? "5840" : "7840"));
            auto       operation = make_resumable_decoder(source)(actual);
            operation(bytes);
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::malformed_structure);
            CHECK(resource.allocations == 0U);
        }
        SUBCASE("nested indefinite child") {
            const auto bytes     = to_bytes(outer + outer);
            auto       operation = make_resumable_decoder(source)(actual);
            operation(bytes);
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::malformed_structure);
            CHECK(resource.allocations == 0U);
        }
        SUBCASE("bounds reject the declared child length") {
            const auto bytes     = to_bytes(outer + major + "840");
            auto       bounded   = as_bounded_size<0, 32>(actual);
            auto       operation = make_resumable_decoder(source)(bounded);
            operation(bytes);
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::size_limit_exceeded);
            CHECK(resource.allocations == 0U);
        }
        SUBCASE("allocator refusal follows a valid child header") {
            resource.refuse      = true;
            const auto bytes     = to_bytes(outer + major + "840");
            auto       operation = make_resumable_decoder(source)(actual);
            operation(bytes);
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::out_of_memory);
            CHECK(resource.allocations == 1U);
        }
        SUBCASE("unrepresentable child length fails before allocator invocation") {
            resource.refuse      = true;
            const auto bytes     = to_bytes(outer + major + "bffffffffffffffff");
            auto       operation = make_resumable_decoder(source)(actual);
            operation(bytes);
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::out_of_memory);
            CHECK(resource.allocations == 0U);
        }
        CHECK(actual.empty());
    }

    TEST_CASE("indefinite byte strings retain nonreservable and fixed destination behavior") {
        const auto bytes = to_bytes("5f4201024103ff09");
        SUBCASE("nonreservable sequence") {
            input_chunks          storage;
            auto                  source = make_input_source(storage);
            std::deque<std::byte> actual{std::byte{0xaa}};
            auto                  operation = make_resumable_decoder(source)(actual);
            operation(bytes);
            REQUIRE(operation.done());
            REQUIRE(operation.result());
            CHECK(*operation.result() == 7U);
            CHECK(to_hex(actual) == "aa010203");
            CHECK(to_hex(operation.remaining_input()) == "09");
        }
        SUBCASE("fixed destination rejects indefinite extent") {
            input_chunks storage;
            auto         source = make_input_source(storage);
            std::array   actual{std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc}};
            auto         operation = make_resumable_decoder(source)(actual);
            operation(bytes);
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::unexpected_group_size);
            CHECK(to_hex(actual) == "aabbcc");
        }
    }

    TEST_CASE_TEMPLATE("integer maps commit keys and complete pairs at every input boundary", Map, ordered_integer_map,
                       unordered_integer_map) {
        for (const bool indefinite : {false, true}) {
            CAPTURE(indefinite);
            const auto bytes = to_bytes(indefinite ? "bf181819010018191a00010000ff09" : "a2181819010018191a0001000009");
            const std::span<const std::byte> input{bytes};
            const auto                       item_size = bytes.size() - 1;
            for (std::size_t cut = 0; cut < item_size; ++cut) {
                CAPTURE(cut);
                input_chunks storage;
                auto         source = make_input_source(storage);
                Map          actual{{99, 100}};
                auto         operation = make_resumable_decoder(source)(actual);
                operation(input.first(cut));
                REQUIRE_FALSE(operation.done());

                Map committed{{99, 100}};
                if (cut >= 6)
                    committed.emplace(24, 256);
                if (cut >= 13)
                    committed.emplace(25, 65536);
                CHECK(actual == committed);
                std::size_t committed_bytes{};
                for (const auto boundary : {1U, 3U, 6U, 8U, 13U}) {
                    if (boundary <= cut)
                        committed_bytes = boundary;
                }
                CHECK(to_hex(operation.remaining_input()) == to_hex(input.subspan(committed_bytes, cut - committed_bytes)));

                operation(std::span<const std::byte>{}, input.subspan(cut));
                REQUIRE(operation.done());
                REQUIRE(operation.result());
                CHECK(*operation.result() == item_size);
                CHECK(actual == Map{{24, 256}, {25, 65536}, {99, 100}});
                CHECK(to_hex(operation.remaining_input()) == "09");
            }
        }
    }

    TEST_CASE("integer map encoding publishes each complete key and value across output boundaries") {
        const ordered_integer_map expected{{24, 256}, {25, 65536}};
        auto                      check = [&](auto &value, std::string_view hex) {
            const auto wire = to_bytes(hex);
            for (std::size_t cut = 0; cut <= wire.size(); ++cut) {
                CAPTURE(hex);
                CAPTURE(cut);
                std::vector<std::byte> bytes(wire.size());
                output_chunks          storage;
                auto                   sink      = make_output_source(storage);
                auto                   operation = make_resumable_encoder(sink)(value);
                operation(std::span{bytes}.first(cut));
                std::size_t committed_bytes{};
                for (const auto boundary : {1U, 3U, 6U, 8U, 13U, 14U}) {
                    if (boundary <= cut && boundary <= wire.size())
                        committed_bytes = boundary;
                }
                CHECK(to_hex(operation.produced()) == to_hex(std::span{wire}.first(committed_bytes)));
                if (!operation.done())
                    operation(std::span{bytes}.subspan(cut));
                REQUIRE(operation.done());
                REQUIRE(operation.result());
                CHECK(*operation.result() == wire.size());
                CHECK(bytes == wire);
            }
        };
        check(expected, "a2181819010018191a00010000");
        auto indefinite = as_indefinite{expected};
        check(indefinite, "bf181819010018191a00010000ff");
    }

    TEST_CASE("integer map eof retains completed keys and publishes only complete pairs") {
        for (const auto hex : {"a218181901", "bf18181901"}) {
            CAPTURE(hex);
            const auto          bytes = to_bytes(hex);
            input_chunks        storage;
            auto                source = make_input_source(storage);
            ordered_integer_map actual{{99, 100}};
            auto                operation = make_resumable_decoder(source)(actual);
            operation(bytes);
            REQUIRE_FALSE(operation.done());
            CHECK(actual == ordered_integer_map{{99, 100}});
            CHECK(to_hex(operation.remaining_input()) == "1901");
            operation.finish();
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::incomplete);
            CHECK(actual == ordered_integer_map{{99, 100}});
            CHECK(to_hex(operation.remaining_input()) == "1901");
        }
    }

    TEST_CASE("integer map bounds count duplicate wire pairs and accept the final break") {
        const auto bytes = to_bytes("bf01020103ff09");
        auto       check = [&]<std::size_t Minimum, std::size_t Maximum>(status_code expected_status, std::uint64_t expected_value,
                                                                         std::string_view tail) {
            input_chunks        storage;
            auto                source = make_input_source(storage);
            ordered_integer_map actual;
            auto                bounded   = as_bounded_size<Minimum, Maximum>(actual);
            auto                operation = make_resumable_decoder(source)(bounded);
            operation(bytes);
            REQUIRE(operation.done());
            const auto result = operation.result();
            CHECK((result ? status_code::success : result.error()) == expected_status);
            CHECK(actual == ordered_integer_map{{1, expected_value}});
            CHECK(to_hex(operation.remaining_input()) == tail);
            if (result)
                CHECK(*result == 6U);
        };
        check.template operator()<2, 2>(status_code::success, 3, "09");
        check.template operator()<3, 3>(status_code::size_limit_exceeded, 3, "09");
        check.template operator()<0, 1>(status_code::size_limit_exceeded, 2, "0103ff09");
    }

    TEST_CASE("integer map failures retain the malformed key or value after a successful pair") {
        struct failure {
            std::string_view hex;
            std::string_view tail;
            status_code      status;
        };
        const failure cases[]{
            {"bf010202ff090000000000000000", "ff090000000000000000", status_code::malformed_structure},
            {"bf01021cff090000000000000000", "1cff090000000000000000", status_code::invalid_additional_info},
            {"a20102021b000000010000000009", "1b000000010000000009", status_code::no_match_for_uint_on_buffer},
            {"bf0102", "", status_code::incomplete},
        };
        for (const auto &test : cases) {
            CAPTURE(test.hex);
            const auto                             bytes = to_bytes(test.hex);
            input_chunks                           storage;
            auto                                   source = make_input_source(storage);
            std::map<std::uint32_t, std::uint32_t> actual;
            auto operation = make_resumable_decoder_with_options<strict_integer_decoder_options>(source)(actual);
            operation(bytes, end_of_input);
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == test.status);
            CHECK(actual == std::map<std::uint32_t, std::uint32_t>{{1, 2}});
            CHECK(to_hex(operation.remaining_input()) == test.tail);
        }
    }

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
