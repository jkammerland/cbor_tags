#include "test_util.h"

#include <array>
#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <deque>
#include <iterator>
#include <list>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

using namespace cbor::tags;

namespace {

using validation_chunks = std::list<std::span<const std::byte>>;

template <typename Storage>
void check_validation_error(Storage &storage, status_code expected_status, std::size_t expected_offset, std::string_view expected_tail,
                            validation_options options = {}) {
    auto source = make_input_source(storage);
    source.close();
    auto operation = make_resumable_validator(source, 1, options);

    REQUIRE(operation.done());
    REQUIRE_FALSE(operation.result());
    CHECK(operation.result().error() == expected_status);
    CHECK(source.committed_position().offset() == expected_offset);
    CHECK(to_hex(operation.remaining_input()) == expected_tail);
}

template <typename Bytes, typename Storage> void check_noncontiguous_validation() {
    const auto wire = to_bytes("878200019f0203ffa10102bf0304ffd864c0055f4200014102ff7f62c3a963e282acffff1c");
    Bytes      bytes(wire.begin(), wire.end());
    using chunk = std::ranges::subrange<typename Bytes::const_iterator>;
    static_assert(std::ranges::borrowed_range<chunk>);
    static_assert(!std::ranges::contiguous_range<chunk>);

    const auto first = std::next(bytes.cbegin(), 7);
    const auto last  = std::next(bytes.cbegin(), 33);
    Storage    storage;
    auto       source    = make_input_source(storage);
    auto       operation = make_resumable_validator(source);

    // Admit multiple borrowed chunks together, including empty descriptors and
    // a UTF-8 chunk boundary. The reader must cross them in the same resume.
    const std::array initial{chunk{bytes.cbegin(), first}, chunk{first, first}, chunk{first, last}};
    operation(initial);
    REQUIRE_FALSE(operation.done());
    CHECK(source.committed_position().offset() == 33U);
    CHECK(to_hex(operation.remaining_input()).empty());

    const std::array final{chunk{last, last}, chunk{last, bytes.cend()}};
    operation(final, end_of_input);
    REQUIRE(operation.done());
    REQUIRE(operation.result());
    CHECK(*operation.result() == 35U);
    CHECK(source.committed_position().offset() == 35U);
    CHECK(to_hex(operation.remaining_input()) == "ff1c");
}

} // namespace

TEST_SUITE("cbor_wire/resumable") {

    TEST_CASE("validation commits complete tokens and partial utf8 payloads at every split") {
        const auto bytes = to_bytes("8c0018181901001a000100001b0102030405060708f93c00fa3f800000fb3ff0000000000000f820d9010080"
                                    "6a61c3a9e282acf09f988043000102ff1c");
        const std::span<const std::byte> input{bytes};

        // Scalar/header tokens commit atomically; definite payload bytes commit
        // as they arrive, even within a multibyte UTF-8 code point.
        constexpr std::array boundaries{0U,  1U,  2U,  4U,  7U,  12U, 21U, 24U, 29U, 38U, 40U, 43U, 44U, 45U,
                                        46U, 47U, 48U, 49U, 50U, 51U, 52U, 53U, 54U, 55U, 56U, 57U, 58U, 59U};

        for (std::size_t cut = 0; cut < boundaries.back(); ++cut) {
            CAPTURE(cut);
            std::size_t committed{};
            for (const auto boundary : boundaries) {
                if (boundary <= cut)
                    committed = boundary;
            }

            validation_chunks storage;
            auto              source    = make_input_source(storage);
            auto              operation = make_resumable_validator(source);
            operation(input.first(cut));
            REQUIRE_FALSE(operation.done());
            CHECK(source.committed_position().offset() == committed);
            CHECK(to_hex(operation.remaining_input()) == to_hex(input.subspan(committed, cut - committed)));

            operation(input.subspan(cut), end_of_input);
            REQUIRE(operation.done());
            REQUIRE(operation.result());
            CHECK(*operation.result() == 59U);
            CHECK(source.committed_position().offset() == 59U);
            CHECK(to_hex(operation.remaining_input()) == "ff1c");

            // EOF changes suspension to incomplete without consuming a partial
            // token or discarding bytes already committed from a payload.
            validation_chunks truncated_storage;
            auto              truncated_source = make_input_source(truncated_storage);
            auto              truncated        = make_resumable_validator(truncated_source);
            truncated(input.first(cut), end_of_input);
            REQUIRE(truncated.done());
            REQUIRE_FALSE(truncated.result());
            CHECK(truncated.result().error() == status_code::incomplete);
            CHECK(truncated_source.committed_position().offset() == committed);
            CHECK(to_hex(truncated.remaining_input()) == to_hex(input.subspan(committed, cut - committed)));
        }
    }

    TEST_CASE("validation distinguishes malformed tokens from consumed structural errors") {
        struct failure {
            std::string_view suffix;
            status_code      status;
            std::size_t      committed;
        };

        // Each second array element follows a complete nine-byte integer.
        // Long tails keep the offending token inside a contiguous input window.
        constexpr std::array cases{
            failure{"1c", status_code::invalid_additional_info, 10},   failure{"fc", status_code::invalid_additional_info, 10},
            failure{"f800", status_code::malformed_structure, 10},     failure{"f81f", status_code::malformed_structure, 10},
            failure{"ff", status_code::malformed_structure, 11},       failure{"81ff", status_code::malformed_structure, 12},
            failure{"a1ff", status_code::malformed_structure, 12},     failure{"a101ff", status_code::malformed_structure, 13},
            failure{"bf01ff", status_code::malformed_structure, 13},   failure{"c0ff", status_code::malformed_structure, 12},
            failure{"5f6161ff", status_code::malformed_structure, 12}, failure{"7f7fff", status_code::malformed_structure, 12},
            failure{"7ff800ff", status_code::malformed_structure, 11}, failure{"7f61c361a9ff", status_code::invalid_utf8_sequence, 13},
        };

        for (const auto &test : cases) {
            CAPTURE(test.suffix);
            const auto bytes = to_bytes(std::string{"821b0102030405060708"} + std::string{test.suffix} + "000102030405060708090a");
            const std::span<const std::byte> input{bytes};
            const auto                       tail = to_hex(input.subspan(test.committed));

            // All chunks are admitted before construction. Partitioning at
            // every byte exercises both complete-window and crossing tokens.
            for (std::size_t cut = 0; cut <= input.size(); ++cut) {
                CAPTURE(cut);
                validation_chunks storage{input.first(cut), input.subspan(cut)};
                check_validation_error(storage, test.status, test.committed, tail);
            }
        }
    }

    TEST_CASE("validation consumes invalid utf8 through the declared payload before failing") {
        const auto                       bytes = to_bytes("821b01020304050607086bc080616263646566676869ff1c00010203040506070809");
        const std::span<const std::byte> input{bytes};

        for (std::size_t cut = 11; cut < 22; ++cut) {
            CAPTURE(cut);
            validation_chunks storage;
            auto              source    = make_input_source(storage);
            auto              operation = make_resumable_validator(source);
            operation(input.first(cut));

            REQUIRE_FALSE(operation.done());
            CHECK(source.committed_position().offset() == cut);
            CHECK(to_hex(operation.remaining_input()).empty());

            operation(input.subspan(cut), end_of_input);
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::invalid_utf8_sequence);
            CHECK(source.committed_position().offset() == 22U);
            CHECK(to_hex(operation.remaining_input()) == "ff1c00010203040506070809");
        }

        // A missing continuation byte is incomplete while the declared payload
        // is short; the same bytes are invalid UTF-8 at a complete payload end.
        const auto        incomplete = to_bytes("821b010203040506070863e282");
        validation_chunks incomplete_storage{std::span<const std::byte>{incomplete}};
        check_validation_error(incomplete_storage, status_code::incomplete, 13, "");

        const auto        invalid = to_bytes("821b010203040506070862e282ff1c00010203040506070809");
        validation_chunks invalid_storage{std::span<const std::byte>{invalid}};
        check_validation_error(invalid_storage, status_code::invalid_utf8_sequence, 13, "ff1c00010203040506070809");
    }

    TEST_CASE("validation depth errors commit the rejected container or tag header") {
        for (const auto suffix : {"d864c000", "d8648100", "d864a000", "d8646000"}) {
            CAPTURE(suffix);
            const auto                       bytes = to_bytes(std::string{"821b0102030405060708"} + suffix + "010203040506070809");
            const std::span<const std::byte> input{bytes};
            validation_chunks                storage{input};
            check_validation_error(storage, status_code::size_limit_exceeded, 13, to_hex(input.subspan(13)), {.max_depth = 2});
        }

        // Scalars at the limit need no further nesting level.
        const auto        bytes = to_bytes("821b0102030405060708d86400ff1c00010203040506070809");
        validation_chunks storage{std::span<const std::byte>{bytes}};
        auto              source    = make_input_source(storage);
        auto              operation = make_resumable_validator(source, 1, {.max_depth = 2});
        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(*operation.result() == 13U);
        CHECK(source.committed_position().offset() == 13U);
        CHECK(to_hex(operation.remaining_input()) == "ff1c00010203040506070809");
    }

    TEST_CASE("validation publishes completed tokens and payloads when nested frame allocation fails") {
        struct fixture {
            std::string_view wire;
            std::size_t      initial;
            std::size_t      failed_offset;
            std::size_t      complete_offset;
            std::string_view error_tail;
            std::string_view resumed_tail;
        };
        constexpr std::array cases{
            fixture{"840001810002ff1c00010203040506070809", 1, 4, 6, "0002ff1c00010203040506070809", "02ff1c00010203040506070809"},
            fixture{"82636162638100ff1c00010203040506070809", 2, 6, 7, "00ff1c00010203040506070809", "ff1c00010203040506070809"},
        };

        for (const auto &test : cases) {
            CAPTURE(test.wire);
            const auto                       bytes = to_bytes(test.wire);
            const std::span<const std::byte> input{bytes};
            validation_chunks                storage;
            auto                             source = make_input_source(storage);

            {
                auto operation = make_resumable_validator(source);
                operation(input.first(test.initial));
                REQUIRE_FALSE(operation.done());
                CHECK(source.committed_position().offset() == test.initial);

                // Admit descriptors before injection. Suspend inside the token
                // or payload reader so its frame already exists. A scalar or
                // payload and the following array header must survive failure
                // to allocate that array's nested sequence frame.
                source.append(input.subspan(test.initial));
                {
                    cbor::tags::test::detail::allocation_failure_guard guard;
                    operation(end_of_input);
                }

                REQUIRE(operation.done());
                if constexpr (cbor::tags::test::detail::allocation_failure_guard_enabled) {
                    REQUIRE_FALSE(operation.result());
                    CHECK(operation.result().error() == status_code::out_of_memory);
                    CHECK(source.committed_position().offset() == test.failed_offset);
                    CHECK(to_hex(operation.remaining_input()) == test.error_tail);
                } else {
                    MESSAGE("Allocation failure injection is unavailable; checking normal completion only.");
                    REQUIRE(operation.result());
                    CHECK(*operation.result() == test.complete_offset);
                    CHECK(source.committed_position().offset() == test.complete_offset);
                    CHECK(to_hex(operation.remaining_input()) == "ff1c00010203040506070809");
                }
            }

            // After the failed operation releases the source, a new request
            // starts at its published tail without replaying consumed bytes.
            if constexpr (cbor::tags::test::detail::allocation_failure_guard_enabled) {
                auto tail = make_resumable_validator(source);
                REQUIRE(tail.done());
                REQUIRE(tail.result());
                CHECK(*tail.result() == 1U);
                CHECK(source.committed_position().offset() == test.failed_offset + 1);
                CHECK(to_hex(tail.remaining_input()) == test.resumed_tail);
            }
        }
    }

    TEST_CASE("validation item counts stop at committed roots and permit a zero count") {
        const auto                       bytes = to_bytes("1b01020304050607081818ff1c00010203040506070809");
        const std::span<const std::byte> input{bytes};
        validation_chunks                storage{input};
        auto                             source = make_input_source(storage);

        {
            auto operation = make_resumable_validator(source, 0);
            REQUIRE(operation.done());
            REQUIRE(operation.result());
            CHECK(*operation.result() == 0U);
            CHECK(source.committed_position().offset() == 0U);
            CHECK(to_hex(operation.remaining_input()) == to_hex(input));
        }
        {
            auto operation = make_resumable_validator(source, 2);
            REQUIRE(operation.done());
            REQUIRE(operation.result());
            CHECK(*operation.result() == 11U);
            CHECK(source.committed_position().offset() == 11U);
            CHECK(to_hex(operation.remaining_input()) == "ff1c00010203040506070809");
        }
        {
            // A later operation starts at the committed root boundary and
            // consumes the structural break before returning its error.
            auto operation = make_resumable_validator(source);
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::malformed_structure);
            CHECK(source.committed_position().offset() == 12U);
            CHECK(to_hex(operation.remaining_input()) == "1c00010203040506070809");
        }
    }

    TEST_CASE("validation until eof publishes completed roots while waiting for the next token") {
        const auto                       bytes = to_bytes("1b01020304050607081818f8209f0001ff");
        const std::span<const std::byte> input{bytes};
        validation_chunks                storage;
        auto                             source    = make_input_source(storage);
        auto                             operation = make_resumable_validator(source, until_end_of_input);

        operation(input.first(10));
        REQUIRE_FALSE(operation.done());
        CHECK(source.committed_position().offset() == 9U);
        CHECK(to_hex(operation.remaining_input()) == "18");

        const std::array chunks{input.subspan(10, 2), input.subspan(12, 0), input.subspan(12)};
        operation(chunks);
        REQUIRE_FALSE(operation.done());
        CHECK(source.committed_position().offset() == 17U);
        CHECK(to_hex(operation.remaining_input()).empty());

        SUBCASE("clean eof completes the admitted sequence") {
            operation(end_of_input);
            REQUIRE(operation.done());
            REQUIRE(operation.result());
            CHECK(*operation.result() == 17U);
            CHECK(source.committed_position().offset() == 17U);
            CHECK(to_hex(operation.remaining_input()).empty());
        }
        SUBCASE("truncated next token remains unread at eof") {
            const auto truncated = to_bytes("1a0001");
            operation(truncated, end_of_input);
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::incomplete);
            CHECK(source.committed_position().offset() == 17U);
            CHECK(to_hex(operation.remaining_input()) == "1a0001");
        }
    }

    TEST_CASE("validation crosses preadmitted contiguous chunks and dynamic descriptor storage") {
        const auto                       bytes = to_bytes("878200019f0203ffa10102bf0304ffd864c0055f4200014102ff7f62c3a963e282acffff1c");
        const std::span<const std::byte> input{bytes};

        for (std::size_t cut = 0; cut <= 35; ++cut) {
            CAPTURE(cut);
            std::deque<std::span<const std::byte>> storage{input.first(cut), input.subspan(cut, 0), input.subspan(cut)};
            auto                                   source    = make_input_source(storage);
            auto                                   operation = make_resumable_validator(source);

            REQUIRE(operation.done());
            REQUIRE(operation.result());
            CHECK(*operation.result() == 35U);
            CHECK(source.committed_position().offset() == 35U);
            CHECK(to_hex(operation.remaining_input()) == "ff1c");
        }
    }

    TEST_CASE("validation falls through to borrowed noncontiguous chunks") {
        using list_chunk  = std::ranges::subrange<std::list<std::byte>::const_iterator>;
        using deque_chunk = std::ranges::subrange<std::deque<std::byte>::const_iterator>;
        check_noncontiguous_validation<std::list<std::byte>, std::list<list_chunk>>();
        check_noncontiguous_validation<std::deque<std::byte>, std::deque<deque_chunk>>();
    }
}
