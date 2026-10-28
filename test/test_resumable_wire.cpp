#include "test_util.h"

#include <algorithm>
#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cbor_tags/cbor_traversal.h>
#include <cbor_tags/codec.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <list>
#include <map>
#include <memory_resource>
#include <new>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

using namespace cbor::tags;

namespace {

using input_chunks  = std::list<std::span<const std::byte>>;
using output_chunks = std::list<std::span<std::byte>>;

struct counting_resource : std::pmr::memory_resource {
    std::size_t allocations{};
    std::size_t last_request{};
    bool        refuse_allocation{};

    void *do_allocate(std::size_t bytes, std::size_t alignment) override {
        ++allocations;
        last_request = bytes;
        if (refuse_allocation)
            throw std::bad_alloc{};
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void *pointer, std::size_t bytes, std::size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override { return this == &other; }
};

struct wire_message {
    std::string                name;
    std::vector<std::uint32_t> values;
};

struct empty_group {};
struct single_group {
    int value{};
};
struct two_group {
    int first{};
    int second{};
};

struct explicit_only_tagged {
    static constexpr std::uint64_t cbor_tag = 100;
    std::uint32_t                  value{};

    template <typename Decoder> expected<void, status_code> decode(Decoder &) {
        static_assert(sizeof(Decoder) == 0, "an explicit resumable hook must bypass ordinary decode");
        return {};
    }

    template <typename Context> resumable<status_code> resumable_decode(Context &context) { co_return co_await context(value); }
};

template <typename Self> struct rejecting_optional_bool_codec : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;

    status_code decode(std::optional<bool> &, major_type, std::byte) { return status_code::no_match_for_tag_simple_on_buffer; }
};

struct bool_hook_calls {
    static inline int count{};
};

template <typename Self> struct resumable_bool_codec : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;

    resumable<status_code> resumable_decode(bool &value) {
        ++bool_hook_calls::count;
        const auto status = co_await static_cast<Self &>(*this).ordinary(value);
        if (status == status_code::success)
            value = !value;
        co_return status;
    }
};

template <typename Self> struct rejecting_resumable_bool_codec : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;

    resumable<status_code> resumable_decode(bool &) {
        ++bool_hook_calls::count;
        return status_code::no_match_for_tag_simple_on_buffer;
    }
};

template <typename Self> struct committed_rejecting_bool_codec : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;

    resumable<status_code> resumable_decode(bool &value) {
        ++bool_hook_calls::count;
        const auto status = co_await static_cast<Self &>(*this).ordinary(value);
        co_return status == status_code::success ? status_code::no_match_for_tag_simple_on_buffer : status;
    }
};

status_code validation_status(std::span<const std::byte> bytes, validation_options options = {}) {
    auto       decoder = make_decoder(bytes);
    const auto result  = validate_item(decoder, options);
    return result ? status_code::success : result.error();
}

status_code operation_status(const expected<std::size_t, status_code> &result) { return result ? status_code::success : result.error(); }

} // namespace

TEST_SUITE("cbor_wire/resumable") {

    TEST_CASE("integer arrays commit only complete elements across each wire width") {
        const auto                       bytes = to_bytes("86001718181901001a000100001b010203040506070809");
        const std::vector<std::uint64_t> expected{0, 23, 24, 256, 65536, 0x0102030405060708ULL};
        const std::array<std::size_t, 6> element_ends{2, 3, 5, 8, 13, 22};
        const std::span<const std::byte> input{bytes};

        for (std::size_t cut = 0; cut < element_ends.back(); ++cut) {
            CAPTURE(cut);
            input_chunks               storage;
            auto                       source = make_input_source(storage);
            std::vector<std::uint64_t> actual{99};
            auto                       operation = make_resumable_decoder(source)(actual);
            operation(input.first(cut));
            REQUIRE_FALSE(operation.done());

            std::vector<std::uint64_t> committed{99};
            for (std::size_t index = 0; index < element_ends.size() && element_ends[index] <= cut; ++index)
                committed.push_back(expected[index]);
            CHECK(actual == committed);

            operation(input.subspan(cut));
            REQUIRE(operation.done());
            REQUIRE(operation.result());
            CHECK(*operation.result() == 22U);
            committed.assign(1, 99);
            committed.insert(committed.end(), expected.begin(), expected.end());
            CHECK(actual == committed);
            CHECK(to_hex(operation.remaining_input()) == "09");
        }

        // All admitted chunks are available during the same inline resume.
        input_chunks               storage;
        auto                       source = make_input_source(storage);
        std::vector<std::uint64_t> actual;
        auto                       operation = make_resumable_decoder(source)(actual);
        std::array                 chunks{input.first(4), input.subspan(4, 12), input.subspan(16)};
        operation(chunks);
        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(*operation.result() == 22U);
        CHECK(actual == expected);
        CHECK(to_hex(operation.remaining_input()) == "09");
    }

    TEST_CASE("integer arrays retain wide wire narrowing policy across every split") {
        const auto                       bytes = to_bytes("821b00000000000001001b0000000100000007");
        const std::span<const std::byte> input{bytes};
        for (std::size_t cut = 0; cut < input.size(); ++cut) {
            CAPTURE(cut);
            {
                input_chunks               storage;
                auto                       source = make_input_source(storage);
                std::vector<std::uint32_t> actual;
                auto                       operation = make_resumable_decoder(source)(actual);
                operation(input.first(cut));
                REQUIRE_FALSE(operation.done());
                operation(input.subspan(cut));
                REQUIRE(operation.done());
                REQUIRE(operation.result());
                CHECK(*operation.result() == 19U);
                CHECK(actual == std::vector<std::uint32_t>{256, 7});
            }

            {
                input_chunks               storage;
                auto                       source = make_input_source(storage);
                std::vector<std::uint32_t> actual;
                auto                       operation = make_resumable_decoder_with_options<strict_integer_decoder_options>(source)(actual);
                operation(input.first(cut));
                REQUIRE_FALSE(operation.done());
                operation(input.subspan(cut));
                REQUIRE(operation.done());
                REQUIRE_FALSE(operation.result());
                CHECK(operation.result().error() == status_code::no_match_for_uint_on_buffer);
                CHECK(actual == std::vector<std::uint32_t>{256});
            }
        }
    }

    TEST_CASE("integer array errors retain the committed prefix and offending item") {
        const std::array cases{
            std::pair{"1c", status_code::invalid_additional_info},     std::pair{"1d", status_code::invalid_additional_info},
            std::pair{"1e", status_code::invalid_additional_info},     std::pair{"1f", status_code::invalid_additional_info},
            std::pair{"20", status_code::no_match_for_uint_on_buffer}, std::pair{"f5", status_code::no_match_for_uint_on_buffer},
        };
        for (const auto &[bad_item, status] : cases) {
            CAPTURE(bad_item);
            const auto                 tail  = std::string{bad_item} + "030000000000000000";
            const auto                 bytes = to_bytes("8301" + tail);
            input_chunks               storage;
            auto                       source = make_input_source(storage);
            std::vector<std::uint64_t> actual;
            auto                       operation = make_resumable_decoder(source)(actual);
            operation(bytes);

            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status);
            CHECK(actual == std::vector<std::uint64_t>{1});
            CHECK(to_hex(operation.remaining_input()) == tail);
        }
    }

    TEST_CASE("integer arrays encode into empty and tiny output windows") {
        const std::vector<std::uint64_t> values{0, 23, 24, 256, 65536, 0x0102030405060708ULL};
        for (std::size_t chunk_size = 1; chunk_size <= 9; ++chunk_size) {
            CAPTURE(chunk_size);
            std::array<std::byte, 22> bytes{};
            output_chunks             storage;
            auto                      sink      = make_output_source(storage);
            auto                      operation = make_resumable_encoder(sink)(values);
            operation(std::span<std::byte>{});
            REQUIRE_FALSE(operation.done());
            for (std::size_t offset = 0; offset < bytes.size(); offset += chunk_size)
                operation(std::span{bytes}.subspan(offset, std::min(chunk_size, bytes.size() - offset)));

            REQUIRE(operation.done());
            REQUIRE(operation.result());
            CHECK(*operation.result() == bytes.size());
            CHECK(to_hex(bytes) == "86001718181901001a000100001b0102030405060708");
        }
    }

    TEST_CASE("integer array output eof preserves the completed prefix") {
        const std::vector<std::uint64_t> values{1, std::numeric_limits<std::uint64_t>::max(), 3};
        for (std::size_t partial = 0; partial < 9; ++partial) {
            CAPTURE(partial);
            std::vector<std::byte> bytes(2 + partial);
            output_chunks          storage;
            auto                   sink      = make_output_source(storage);
            auto                   operation = make_resumable_encoder(sink)(values);
            operation(bytes);
            REQUIRE_FALSE(operation.done());
            CHECK(to_hex(operation.produced()) == "8301");

            operation.finish();
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::incomplete);
            CHECK(to_hex(operation.produced()) == "8301");
        }
    }

    TEST_CASE("complete integer array headers reserve without appending elements") {
        const auto                      bytes = to_bytes("83");
        counting_resource               resource;
        std::pmr::vector<std::uint32_t> actual{&resource};
        input_chunks                    storage;
        auto                            source    = make_input_source(storage);
        auto                            operation = make_resumable_decoder(source)(actual);
        operation(bytes);
        REQUIRE_FALSE(operation.done());
        CHECK(actual.empty());
        CHECK(actual.capacity() >= 3U);
        CHECK(resource.allocations == 1U);

        operation.finish();
        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::incomplete);
        CHECK(actual.empty());
        CHECK(actual.capacity() >= 3U);
        CHECK(resource.allocations == 1U);
    }

    TEST_CASE("integer arrays reserve existing prefix and declared count before incremental payload") {
        const auto                      bytes = to_bytes("88000117181818ff19010019ffff1a00010000");
        counting_resource               resource;
        std::pmr::vector<std::uint32_t> actual{&resource};
        actual.reserve(2);
        actual.push_back(91);
        actual.push_back(92);
        resource.allocations = 0;

        input_chunks storage;
        auto         source    = make_input_source(storage);
        auto         operation = make_resumable_decoder(source)(actual);
        operation(std::span<const std::byte>{bytes}.first(1));
        REQUIRE_FALSE(operation.done());
        CHECK(std::vector<std::uint32_t>{actual.begin(), actual.end()} == std::vector<std::uint32_t>{91, 92});
        CHECK(actual.capacity() >= 10U);
        CHECK(resource.allocations == 1U);

        for (const auto &byte : std::span<const std::byte>{bytes}.subspan(1)) {
            operation(std::span{&byte, 1});
            CHECK(resource.allocations == 1U);
        }
        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(*operation.result() == bytes.size());
        CHECK(std::vector<std::uint32_t>{actual.begin(), actual.end()} ==
              std::vector<std::uint32_t>{91, 92, 0, 1, 23, 24, 255, 256, 65535, 65536});
        CHECK(actual.get_allocator().resource() == &resource);
        CHECK(resource.allocations == 1U);
    }

    TEST_CASE("split integer array headers allocate only after the complete header") {
        for (const auto hex : {"9803", "990003", "9a00000003", "9b0000000000000003"}) {
            CAPTURE(hex);
            const auto                       bytes = to_bytes(hex);
            const std::span<const std::byte> header{bytes};
            for (std::size_t cut = 0; cut < header.size(); ++cut) {
                CAPTURE(cut);
                for (const bool finish_header : {false, true}) {
                    CAPTURE(finish_header);
                    counting_resource               resource;
                    std::pmr::vector<std::uint32_t> actual{&resource};
                    input_chunks                    storage;
                    auto                            source    = make_input_source(storage);
                    auto                            operation = make_resumable_decoder(source)(actual);
                    operation(header.first(cut));
                    REQUIRE_FALSE(operation.done());
                    CHECK(actual.empty());
                    CHECK(actual.capacity() == 0U);
                    CHECK(resource.allocations == 0U);

                    if (finish_header) {
                        operation(header.subspan(cut));
                        REQUIRE_FALSE(operation.done());
                        CHECK(actual.empty());
                        CHECK(actual.capacity() >= 3U);
                        CHECK(resource.allocations == 1U);
                    }

                    operation.finish();
                    REQUIRE(operation.done());
                    REQUIRE_FALSE(operation.result());
                    CHECK(operation.result().error() == status_code::incomplete);
                    CHECK(resource.allocations == (finish_header ? 1U : 0U));
                }
            }
        }
    }

    TEST_CASE("malformed integer array headers do not reserve destination storage") {
        for (const auto hex : {"9c", "9d", "9e"}) {
            CAPTURE(hex);
            const auto                      bytes = to_bytes(hex);
            counting_resource               resource;
            std::pmr::vector<std::uint32_t> actual{&resource};
            input_chunks                    storage;
            auto                            source    = make_input_source(storage);
            auto                            operation = make_resumable_decoder(source)(actual);
            operation(bytes);

            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::invalid_additional_info);
            CHECK(actual.empty());
            CHECK(resource.allocations == 0U);
        }
    }

    TEST_CASE("bounded integer arrays reject excess counts before requesting allocation") {
        const auto        bytes = to_bytes("83");
        counting_resource resource;
        resource.refuse_allocation = true;
        std::pmr::vector<std::uint32_t> actual{&resource};
        auto                            bounded = as_bounded_size<0, 2>(actual);
        input_chunks                    storage;
        auto                            source    = make_input_source(storage);
        auto                            operation = make_resumable_decoder(source)(bounded);
        operation(bytes);

        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::size_limit_exceeded);
        CHECK(actual.empty());
        CHECK(resource.allocations == 0U);
    }

    TEST_CASE("integer array reservation failures report out of memory before payload") {
        const auto        bytes = to_bytes("9a00010000");
        counting_resource resource;
        resource.refuse_allocation = true;
        std::pmr::vector<std::uint32_t> actual{&resource};
        input_chunks                    storage;
        auto                            source    = make_input_source(storage);
        auto                            operation = make_resumable_decoder(source)(actual);
        operation(bytes);

        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::out_of_memory);
        CHECK(actual.empty());
        CHECK(actual.capacity() == 0U);
        CHECK(resource.allocations == 1U);
        CHECK(resource.last_request >= 65536U * sizeof(std::uint32_t));
    }

    TEST_CASE("empty integer arrays do not allocate destination storage") {
        const auto        bytes = to_bytes("80");
        counting_resource resource;
        resource.refuse_allocation = true;
        std::pmr::vector<std::uint32_t> actual{&resource};
        input_chunks                    storage;
        auto                            source    = make_input_source(storage);
        auto                            operation = make_resumable_decoder(source)(actual);
        operation(bytes);

        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(*operation.result() == 1U);
        CHECK(actual.empty());
        CHECK(resource.allocations == 0U);
    }

    TEST_CASE("unrepresentable integer array reservation preserves prefix without calling allocator") {
        const auto                      bytes = to_bytes("9bffffffffffffffff");
        counting_resource               resource;
        std::pmr::vector<std::uint32_t> actual{&resource};
        actual.push_back(91);
        const auto capacity        = actual.capacity();
        resource.allocations       = 0;
        resource.refuse_allocation = true;
        input_chunks storage;
        auto         source    = make_input_source(storage);
        auto         operation = make_resumable_decoder(source)(actual);
        operation(bytes);

        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::out_of_memory);
        CHECK(std::vector<std::uint32_t>{actual.begin(), actual.end()} == std::vector<std::uint32_t>{91});
        CHECK(actual.capacity() == capacity);
        CHECK(resource.allocations == 0U);
    }

    TEST_CASE("truncated integer array payload retains prefix and reserved capacity") {
        const auto                      bytes = to_bytes("83011901");
        counting_resource               resource;
        std::pmr::vector<std::uint32_t> actual{&resource};
        input_chunks                    storage;
        auto                            source    = make_input_source(storage);
        auto                            operation = make_resumable_decoder(source)(actual);
        operation(bytes);
        REQUIRE_FALSE(operation.done());
        CHECK(std::vector<std::uint32_t>{actual.begin(), actual.end()} == std::vector<std::uint32_t>{1});
        CHECK(actual.capacity() >= 3U);
        CHECK(resource.allocations == 1U);
        const auto capacity = actual.capacity();

        operation.finish();
        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::incomplete);
        CHECK(std::vector<std::uint32_t>{actual.begin(), actual.end()} == std::vector<std::uint32_t>{1});
        CHECK(actual.capacity() == capacity);
        CHECK(resource.allocations == 1U);
    }

    TEST_CASE("fixed integer array extents reject mismatched headers without changing storage") {
        const auto                      bytes = to_bytes("83");
        counting_resource               resource;
        std::pmr::vector<std::uint32_t> backing{&resource};
        backing.assign({91, 92});
        resource.allocations = 0;
        std::span<std::uint32_t, 2> actual{backing.data(), 2};
        input_chunks                storage;
        auto                        source    = make_input_source(storage);
        auto                        operation = make_resumable_decoder(source)(actual);
        operation(bytes);

        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::unexpected_group_size);
        CHECK(std::vector<std::uint32_t>{backing.begin(), backing.end()} == std::vector<std::uint32_t>{91, 92});
        CHECK(resource.allocations == 0U);
    }

    TEST_CASE("variant alternatives do not instantiate bypassed ordinary customizations") {
        const auto bytes = to_bytes("d86401");
        for (std::size_t cut = 0; cut <= bytes.size(); ++cut) {
            CAPTURE(cut);
            input_chunks                             storage;
            auto                                     source = make_input_source(storage);
            std::variant<explicit_only_tagged, bool> actual{false};
            auto                                     operation = make_resumable_decoder(source)(actual);
            operation(std::span<const std::byte>{bytes}.first(cut));
            if (!operation.done())
                operation(std::span<const std::byte>{bytes}.subspan(cut), end_of_input);

            REQUIRE(operation.result());
            CHECK(*operation.result() == bytes.size());
            REQUIRE(std::holds_alternative<explicit_only_tagged>(actual));
            CHECK(std::get<explicit_only_tagged>(actual).value == 1U);
        }
    }

    TEST_CASE("variant mismatches retain ordinary status across every split") {
        auto check = []<typename T>(std::type_identity<T>, std::string_view hex, status_code expected_status) {
            const auto bytes = to_bytes(hex);
            T          ordinary;
            const auto reference = make_decoder_with_options<strict_integer_decoder_options>(bytes)(ordinary);
            REQUIRE_FALSE(reference);
            REQUIRE(reference.error() == expected_status);

            for (std::size_t cut = 0; cut <= bytes.size(); ++cut) {
                CAPTURE(hex);
                CAPTURE(cut);
                input_chunks storage;
                auto         source = make_input_source(storage);
                T            actual;
                auto         operation = make_resumable_decoder_with_options<strict_integer_decoder_options>(source)(actual);
                operation(std::span<const std::byte>{bytes}.first(cut));
                if (!operation.done())
                    operation(std::span<const std::byte>{bytes}.subspan(cut), end_of_input);

                REQUIRE(operation.done());
                REQUIRE_FALSE(operation.result());
                CHECK(operation.result().error() == expected_status);
            }
        };

        using narrow_variant = std::variant<std::uint8_t, bool>;
        using tagged_variant = std::variant<std::tuple<static_tag<100>, int>, bool>;
        check(std::type_identity<narrow_variant>{}, "190100", status_code::no_match_in_variant_on_buffer);
        check(std::type_identity<tagged_variant>{}, "d8646161", status_code::no_match_in_variant_on_buffer);
        check(std::type_identity<narrow_variant>{}, "1901", status_code::incomplete);
        check(std::type_identity<narrow_variant>{}, "1c", status_code::invalid_additional_info);
    }

    TEST_CASE("simple variant catchall preserves ordinary customization fallback") {
        const auto bytes = to_bytes("f5");
        auto       check = [&]<typename T>() {
            T          ordinary;
            const auto reference = make_decoder<rejecting_optional_bool_codec>(bytes)(ordinary);
            REQUIRE(reference);
            REQUIRE(std::holds_alternative<simple>(ordinary));
            CHECK(std::get<simple>(ordinary) == simple{21});

            input_chunks storage;
            auto         source = make_input_source(storage);
            T            actual;
            auto         operation = make_resumable_decoder<rejecting_optional_bool_codec>(source)(actual);
            operation(bytes, end_of_input);

            REQUIRE(operation.result());
            CHECK(*operation.result() == 1U);
            CHECK(actual == ordinary);
        };

        check.template operator()<std::variant<std::optional<bool>, simple>>();
        check.template operator()<std::variant<simple, std::optional<bool>>>();
        check.template operator()<std::variant<std::variant<std::optional<bool>, int>, simple>>();
    }

    TEST_CASE("simple variant dispatch preserves nested and optional explicit hooks") {
        const auto bytes = to_bytes("f5");
        auto       check = [&]<typename T>(const T &expected) {
            input_chunks storage;
            auto         source = make_input_source(storage);
            T            actual;
            const auto   calls     = bool_hook_calls::count;
            auto         operation = make_resumable_decoder<resumable_bool_codec>(source)(actual);
            operation(bytes, end_of_input);

            REQUIRE(operation.result());
            CHECK(*operation.result() == 1U);
            CHECK(actual == expected);
            CHECK(bool_hook_calls::count == calls + 1);
        };

        check(std::variant<bool, simple>{false});
        check(std::variant<std::variant<bool, int>, simple>{std::variant<bool, int>{false}});
        check(std::variant<std::optional<bool>, simple>{std::optional<bool>{false}});
    }

    TEST_CASE("explicit variant rejection permits catchall only before committed progress") {
        const auto bytes = to_bytes("f5");

        SUBCASE("tentative rejection permits catchall") {
            input_chunks               storage;
            auto                       source = make_input_source(storage);
            std::variant<bool, simple> actual;
            const auto                 calls     = bool_hook_calls::count;
            auto                       operation = make_resumable_decoder<rejecting_resumable_bool_codec>(source)(actual);
            operation(bytes, end_of_input);

            REQUIRE(operation.result());
            CHECK(*operation.result() == 1U);
            REQUIRE(std::holds_alternative<simple>(actual));
            CHECK(std::get<simple>(actual) == simple{21});
            CHECK(bool_hook_calls::count == calls + 1);
        }

        SUBCASE("committed rejection is terminal") {
            input_chunks               storage;
            auto                       source = make_input_source(storage);
            std::variant<bool, simple> actual;
            const auto                 calls     = bool_hook_calls::count;
            auto                       operation = make_resumable_decoder<committed_rejecting_bool_codec>(source)(actual);
            operation(bytes);

            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::no_match_in_variant_on_buffer);
            CHECK(std::ranges::empty(operation.remaining_input()));
            CHECK(bool_hook_calls::count == calls + 1);
        }
    }

    TEST_CASE("reflected message resumes inside text and integer arguments") {
        const auto   first  = to_bytes("826361");
        const auto   second = to_bytes("6263820118");
        const auto   third  = to_bytes("18");
        input_chunks storage;
        auto         source = make_input_source(storage);
        wire_message value;
        auto         operation = make_resumable_decoder(source)(value);

        operation(first);
        CHECK_FALSE(operation.done());
        operation(second);
        CHECK_FALSE(operation.done());
        operation(third);

        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(*operation.result() == 9U);
        CHECK(value.name == "abc");
        CHECK(value.values == std::vector<std::uint32_t>{1, 24});
    }

    TEST_CASE("primitive retry retains its initial byte and counts only once") {
        const auto    first  = to_bytes("1b010203");
        const auto    second = to_bytes("0405060708");
        input_chunks  storage;
        auto          source = make_input_source(storage);
        std::uint64_t value{};
        auto          operation = make_resumable_decoder(source)(value);

        operation(first);
        REQUIRE_FALSE(operation.done());
        CHECK(to_hex(operation.remaining_input()) == "1b010203");
        operation(second);

        REQUIRE(operation.result());
        CHECK(*operation.result() == 9U);
        CHECK(value == UINT64_C(0x0102030405060708));
        CHECK(std::ranges::empty(operation.remaining_input()));
    }

    TEST_CASE("ordinary decoding remains terminal and preserves its decoded prefix") {
        const auto                 bytes = to_bytes("820119ff");
        const std::list<std::byte> storage{bytes.begin(), bytes.end()};
        const auto                 input = std::ranges::subrange{storage.begin(), storage.end()};
        static_assert(!std::ranges::sized_range<decltype(input)>);
        auto             decoder = make_decoder(input);
        std::vector<int> value;
        const auto       result = decoder(value);

        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::incomplete);
        CHECK(value == std::vector<int>{1});
        CHECK(decoder.tell() != input.begin());
    }

    TEST_CASE("variadic and runtime feeds retain the full ordered suffix") {
        const auto                       bytes = to_bytes("1801090a0b");
        const std::span<const std::byte> input{bytes};
        input_chunks                     storage;
        auto                             source = make_input_source(storage);
        std::uint64_t                    value{};
        auto                             operation = make_resumable_decoder(source)(value);

        SUBCASE("variadic") { operation(input.first(1), input.subspan(1, 2), input.subspan(3), end_of_input); }
        SUBCASE("runtime range") {
            const std::array chunks{input.first(1), input.subspan(1, 2), input.subspan(3)};
            operation(std::span{chunks}, end_of_input);
        }

        REQUIRE(operation.result());
        CHECK(*operation.result() == 2U);
        CHECK(value == 1U);
        CHECK(to_hex(operation.remaining_input()) == "090a0b");
    }

    TEST_CASE("eof tokens close input after every ordered chunk in the supplied batch") {
        const auto                       bytes = to_bytes("1a01020304090a");
        const std::span<const std::byte> input{bytes};
        input_chunks                     storage;
        auto                             source = make_input_source(storage);
        std::uint32_t                    value{};
        auto                             operation = make_resumable_decoder(source)(value);

        SUBCASE("eof before chunks") { operation(end_of_input, input.first(2), input.subspan(2)); }
        SUBCASE("eof between chunks") { operation(input.first(2), end_of_input, input.subspan(2)); }
        SUBCASE("eof before runtime batch") {
            const std::array chunks{input.first(2), input.subspan(2, 2), input.subspan(4)};
            operation(end_of_input, std::span{chunks});
        }

        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(*operation.result() == 5U);
        CHECK(value == UINT32_C(0x01020304));
        CHECK(to_hex(operation.remaining_input()) == "090a");
    }

    TEST_CASE("empty chunks do not signal eof and explicit eof terminates incomplete values") {
        const auto    bytes = to_bytes("1a0001");
        input_chunks  storage;
        auto          source = make_input_source(storage);
        std::uint32_t value{};
        auto          operation = make_resumable_decoder(source)(value);

        operation(bytes);
        REQUIRE_FALSE(operation.done());
        operation(std::span<const std::byte>{});
        CHECK_FALSE(operation.done());
        operation(end_of_input);

        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::incomplete);
    }

    TEST_CASE("every truncated message prefix terminates incomplete at eof") {
        const auto                       bytes = to_bytes("8263616263820118");
        const std::span<const std::byte> input{bytes};

        for (std::size_t length = 0; length <= input.size(); ++length) {
            CAPTURE(length);
            input_chunks storage;
            auto         source = make_input_source(storage);
            wire_message value;
            auto         operation = make_resumable_decoder(source)(value);
            operation(input.first(length), end_of_input);

            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::incomplete);
        }
    }

    TEST_CASE("malformed primitive errors are terminal without eof") {
        const auto    bytes = to_bytes("1c");
        input_chunks  storage;
        auto          source = make_input_source(storage);
        std::uint64_t value{};
        auto          operation = make_resumable_decoder(source)(value);
        operation(bytes);

        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::invalid_additional_info);
    }

    TEST_CASE("bounded decode rejects a declared array larger than its bound") {
        const auto       bytes = to_bytes("83");
        input_chunks     storage;
        auto             source = make_input_source(storage);
        std::vector<int> value;
        auto             bounded   = as_bounded_size<0, 2>(value);
        auto             operation = make_resumable_decoder(source)(bounded);
        operation(bytes);

        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::size_limit_exceeded);
        CHECK(value.empty());
    }

    TEST_CASE("explicit indefinite destinations reject definite wire shapes") {
        const auto       bytes = to_bytes("82011818");
        input_chunks     storage;
        auto             source = make_input_source(storage);
        std::vector<int> actual;
        auto             indefinite = as_indefinite{actual};

        SUBCASE("unbounded wrapper") {
            auto operation = make_resumable_decoder(source)(indefinite);
            operation(bytes, end_of_input);
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::no_match_for_array_on_buffer);
        }
        SUBCASE("bounded wrapper") {
            auto bounded   = as_bounded_size<0, 3>(indefinite);
            auto operation = make_resumable_decoder(source)(bounded);
            operation(bytes, end_of_input);
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::no_match_for_array_on_buffer);
        }

        CHECK(actual.empty());
    }

    TEST_CASE("bounded indefinite destinations preserve complete prefixes and size errors") {
        const auto       bytes = to_bytes("9f011818190100ff");
        input_chunks     storage;
        auto             source = make_input_source(storage);
        std::vector<int> actual;
        auto             indefinite = as_indefinite{actual};
        auto             bounded    = as_bounded_size<0, 2>(indefinite);
        auto             operation  = make_resumable_decoder(source)(bounded);

        operation(std::span<const std::byte>{bytes}.first(3));
        REQUIRE_FALSE(operation.done());
        operation(std::span<const std::byte>{bytes}.subspan(3), end_of_input);

        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::size_limit_exceeded);
        CHECK(actual == std::vector<int>{1, 24});
    }

    TEST_CASE("bounded headers consume only their header and preserve ordinary size checks") {
        input_chunks storage;
        auto         source = make_input_source(storage);
        as_array_any header;
        auto         bounded   = as_bounded_size<1, 3>(header);
        auto         operation = make_resumable_decoder(source)(bounded);

        SUBCASE("definite") {
            const auto bytes = to_bytes("820102");
            operation(bytes);
            REQUIRE(operation.result());
            CHECK(*operation.result() == 1U);
            CHECK(header.size == 2U);
            CHECK_FALSE(header.indefinite);
            CHECK(to_hex(operation.remaining_input()) == "0102");
        }
        SUBCASE("indefinite") {
            const auto bytes = to_bytes("9fff");
            operation(bytes);
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::size_limit_exceeded);
        }
    }

    TEST_CASE("bounded contiguous borrowed views retain ordinary noncontiguous-input status") {
        const auto       bytes = to_bytes("63616263");
        input_chunks     storage;
        auto             source    = make_input_source(storage);
        std::string_view actual    = "unchanged";
        auto             bounded   = as_bounded_size<1, 4>(actual);
        auto             operation = make_resumable_decoder(source)(bounded);
        operation(bytes, end_of_input);

        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::contiguous_view_on_non_contiguous_data);
        CHECK(actual == "unchanged");
    }

    TEST_CASE("fixed and bounded zero-byte entries use the declared finite count") {
        const auto   bytes = to_bytes("82");
        input_chunks storage;
        auto         source = make_input_source(storage);

        SUBCASE("fixed extent") {
            std::array<empty_group, 2> actual;
            auto                       operation = make_resumable_decoder(source)(actual);
            operation(bytes, end_of_input);
            REQUIRE(operation.result());
            CHECK(*operation.result() == 1U);
        }
        SUBCASE("bounded extent") {
            std::vector<empty_group> actual;
            auto                     bounded   = as_bounded_size<2, 2>(actual);
            auto                     operation = make_resumable_decoder(source)(bounded);
            operation(bytes, end_of_input);
            REQUIRE(operation.result());
            CHECK(*operation.result() == 1U);
            CHECK(actual.size() == 2U);
        }
    }

    TEST_CASE("nonnull optional empty groups reject input without a zero-progress success") {
        const auto                 bytes = to_bytes("00");
        std::optional<empty_group> ordinary;
        const auto                 expected = make_decoder(bytes)(ordinary);
        REQUIRE_FALSE(expected);
        REQUIRE(expected.error() == status_code::unexpected_group_size);

        input_chunks               storage;
        auto                       source = make_input_source(storage);
        std::optional<empty_group> actual;
        auto                       operation = make_resumable_decoder(source)(actual);
        operation(bytes, end_of_input);

        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == expected.error());
        CHECK_FALSE(actual);
    }

    TEST_CASE("indefinite optional empty-group elements terminate on zero-progress payloads") {
        const auto                              bytes = to_bytes("9f00ff");
        std::vector<std::optional<empty_group>> ordinary;
        const auto                              expected = make_decoder(bytes)(ordinary);
        REQUIRE_FALSE(expected);
        REQUIRE(expected.error() == status_code::unexpected_group_size);

        input_chunks                            storage;
        auto                                    source = make_input_source(storage);
        std::vector<std::optional<empty_group>> actual;
        auto                                    operation = make_resumable_decoder(source)(actual);
        operation(bytes, end_of_input);

        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == expected.error());
        CHECK(actual.empty());
    }

    TEST_CASE("indefinite structural errors retain ordinary typed-decoder statuses") {
        auto check = []<typename T>(const char *hex) {
            const auto bytes = to_bytes(hex);
            T          ordinary_value;
            const auto ordinary_result = make_decoder(bytes)(ordinary_value);
            REQUIRE_FALSE(ordinary_result);
            REQUIRE(ordinary_result.error() == status_code::malformed_structure);

            input_chunks storage;
            auto         source = make_input_source(storage);
            T            actual;
            auto         operation = make_resumable_decoder(source)(actual);
            for (std::size_t index = 0; index < bytes.size() && !operation.done(); ++index) {
                operation(std::span<const std::byte>{bytes}.subspan(index, 1));
            }

            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == ordinary_result.error());
            CHECK(actual == ordinary_value);
        };

        check.template operator()<std::map<std::string, int>>("bf6161ff");
        check.template operator()<std::vector<std::byte>>("5f6161ff");
        check.template operator()<std::string>("7f4101ff");
    }

    TEST_CASE("group arities and standalone tags retain ordinary wire shapes") {
        empty_group              empty;
        single_group             single{7};
        two_group                pair{1, 2};
        auto                     tag    = static_tag<100>{};
        int                      tagged = 9;
        std::array<std::byte, 7> output{};
        output_chunks            storage;
        auto                     sink      = make_output_source(storage);
        auto                     operation = make_resumable_encoder(sink)(empty, single, pair, tag, tagged);
        operation(std::span{output}.first(3));
        operation(std::span{output}.subspan(3));

        REQUIRE(operation.result());
        CHECK(*operation.result() == output.size());
        CHECK(to_hex(output) == "07820102d86409");
    }

    TEST_CASE("validation matches one-shot status at every split and truncated eof") {
        for (const auto *hex : {"01", "1a01020304", "826268694200ff", "9fa101d864826268694200ffff", "7f62c3a9ff", "7f61c361a9ff",
                                "63e282ac", "62c0af", "f800", "ff", "bf01ff", "5f6161ff", "1c", "5a7fffffff"}) {
            CAPTURE(hex);
            const auto                       bytes = to_bytes(hex);
            const std::span<const std::byte> input{bytes};

            for (std::size_t cut = 0; cut <= input.size(); ++cut) {
                CAPTURE(cut);
                input_chunks truncated_storage;
                auto         truncated_source = make_input_source(truncated_storage);
                auto         truncated        = make_resumable_validator(truncated_source);
                truncated(input.first(cut), end_of_input);

                REQUIRE(truncated.done());
                CHECK(operation_status(truncated.result()) == validation_status(input.first(cut)));

                input_chunks split_storage;
                auto         split_source = make_input_source(split_storage);
                auto         split        = make_resumable_validator(split_source);
                split(input.first(cut));
                if (!split.done()) {
                    split(input.subspan(cut), end_of_input);
                }

                REQUIRE(split.done());
                CHECK(operation_status(split.result()) == validation_status(input));
                if (split.result()) {
                    CHECK(*split.result() == input.size());
                }
            }
        }
    }

    TEST_CASE("validation respects item count and leaves trailing malformed data unread") {
        const auto   bytes = to_bytes("011818ff");
        input_chunks storage;
        auto         source    = make_input_source(storage);
        auto         operation = make_resumable_validator(source, 2);
        operation(bytes, end_of_input);

        REQUIRE(operation.result());
        CHECK(*operation.result() == 3U);
        CHECK(to_hex(operation.remaining_input()) == "ff");
    }

    TEST_CASE("validation until eof distinguishes clean boundaries from incomplete items") {
        input_chunks storage;
        auto         source    = make_input_source(storage);
        auto         operation = make_resumable_validator(source, until_end_of_input);

        SUBCASE("empty sequence") {
            operation(end_of_input);
            REQUIRE(operation.result());
            CHECK(*operation.result() == 0U);
        }
        SUBCASE("complete sequence") {
            const auto bytes = to_bytes("011818");
            operation(bytes);
            CHECK_FALSE(operation.done());
            operation(end_of_input);
            REQUIRE(operation.result());
            CHECK(*operation.result() == 3U);
        }
        SUBCASE("truncated last item") {
            const auto bytes = to_bytes("0118");
            operation(bytes, end_of_input);
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::incomplete);
        }

        CHECK(operation.done());
    }

    TEST_CASE("validation depth limits agree with one-shot traversal") {
        const auto                       bytes = to_bytes("81818100");
        const std::span<const std::byte> input{bytes};
        const validation_options         options{.max_depth = 2};
        input_chunks                     storage;
        auto                             source    = make_input_source(storage);
        auto                             operation = make_resumable_validator(source, 1, options);

        operation(input.first(2));
        if (!operation.done()) {
            operation(input.subspan(2), end_of_input);
        }

        REQUIRE(operation.done());
        CHECK(operation_status(operation.result()) == validation_status(input, options));
    }

    TEST_CASE("generator reports a truncated final item after a successful yield") {
        const auto   bytes = to_bytes("011a0001");
        input_chunks storage;
        auto         source    = make_input_source(storage);
        auto         generator = make_resumable_generator<std::uint32_t>(source);
        auto        &first     = generator.next();
        generator(bytes, end_of_input);

        REQUIRE(first.result());
        CHECK(*first.result() == 1U);
        CHECK(generator.value() == 1U);
        auto &truncated = generator.next();
        REQUIRE(truncated.done());
        REQUIRE_FALSE(truncated.result());
        CHECK(truncated.result().error() == status_code::incomplete);
    }
}
