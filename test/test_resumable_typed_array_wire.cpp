#include "test_util.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cbor_tags/codec/typed_array.h>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <list>
#include <ranges>
#include <span>
#include <vector>

using namespace cbor::tags;

namespace {

using input_chunks  = std::list<std::span<const std::byte>>;
using output_chunks = std::list<std::span<std::byte>>;
using array_type    = rfc8746::typed_array<std::uint16_t>;

template <typename Self> struct rejecting_typed_array_tag : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;

    status_code observe_decoded_cbor_tag(std::uint64_t) { return status_code::size_limit_exceeded; }

    void observe_encoded_cbor_tag(std::uint64_t) { throw cbor::tags::detail::encode_status_exception{status_code::size_limit_exceeded}; }
};

constexpr std::array encoded{std::byte{0xd8}, std::byte{0x45}, std::byte{0x44}, std::byte{0x34},
                             std::byte{0x12}, std::byte{0xcd}, std::byte{0xab}};

void check_bad_typed_array_header(std::initializer_list<std::byte> bytes, status_code expected) {
    for (std::size_t cut = 0; cut <= bytes.size(); ++cut) {
        CAPTURE(cut);
        input_chunks chunks;
        auto         source = make_input_source(chunks);
        array_type   actual{99};
        auto         operation = make_resumable_decoder<codec::typed_array>(source)(actual);
        const auto   input     = std::span{bytes.begin(), bytes.size()};
        operation(input.first(cut));
        if (!operation.done()) {
            operation(input.subspan(cut), end_of_input);
        }
        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == expected);
        CHECK(actual.values() == std::vector<std::uint16_t>{99});
    }
}

} // namespace

TEST_SUITE("cbor_wire/resumable_typed_array") {
    TEST_CASE("big endian floating payloads preserve independently specified nan bits") {
        using float_array = rfc8746::typed_array<float, rfc8746::typed_array_byte_order::big>;
        const std::array  bytes{std::byte{0xd8}, std::byte{0x51}, std::byte{0x48}, std::byte{0x7f}, std::byte{0xc1}, std::byte{0x23},
                                std::byte{0x45}, std::byte{0x80}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
        const float_array expected{std::bit_cast<float>(std::uint32_t{0x7fc12345}), -0.0F};
        for (std::size_t cut = 0; cut <= bytes.size(); ++cut) {
            CAPTURE(cut);
            input_chunks chunks;
            auto         source = make_input_source(chunks);
            float_array  actual;
            auto         decoder = make_resumable_decoder<codec::typed_array>(source)(actual);
            decoder(std::span{bytes}.first(cut));
            if (!decoder.done()) {
                decoder(std::span{bytes}.subspan(cut), end_of_input);
            }
            REQUIRE(decoder.result());
            REQUIRE(actual.values().size() == 2);
            CHECK(std::bit_cast<std::uint32_t>(actual.values()[0]) == 0x7fc12345);
            CHECK(std::bit_cast<std::uint32_t>(actual.values()[1]) == 0x80000000);

            std::array<std::byte, bytes.size()> output;
            output_chunks                       output_storage;
            auto                                sink    = make_output_source(output_storage);
            auto                                encoder = make_resumable_encoder<codec::typed_array>(sink)(expected);
            encoder(std::span{output}.first(cut));
            if (!encoder.done()) {
                encoder(std::span{output}.subspan(cut), end_of_input);
            }
            REQUIRE(encoder.result());
            CHECK(output == bytes);
        }
    }

    TEST_CASE("typed array headers preserve ordinary tag and definite byte string validation") {
        check_bad_typed_array_header({std::byte{0x80}}, status_code::no_match_for_tag_on_buffer);
        check_bad_typed_array_header({std::byte{0xd8}, std::byte{0x46}}, status_code::no_match_for_tag);
        check_bad_typed_array_header({std::byte{0xdf}}, status_code::invalid_additional_info);
        check_bad_typed_array_header({std::byte{0xd8}, std::byte{0x45}, std::byte{0x80}}, status_code::no_match_for_bstr_on_buffer);
        check_bad_typed_array_header({std::byte{0xd8}, std::byte{0x45}, std::byte{0x5f}}, status_code::no_match_for_bstr_on_buffer);
        check_bad_typed_array_header({std::byte{0xd8}, std::byte{0x45}, std::byte{0x5c}}, status_code::invalid_additional_info);
        check_bad_typed_array_header({std::byte{0xd8}, std::byte{0x45}, std::byte{0x43}}, status_code::unexpected_group_size);
    }

    TEST_CASE("ordinary tag observer rejection propagates through resumable typed array headers") {
        for (std::size_t cut = 0; cut <= encoded.size(); ++cut) {
            CAPTURE(cut);
            input_chunks chunks;
            auto         source = make_input_source(chunks);
            array_type   actual{99};
            auto         decoder = make_resumable_decoder<rejecting_typed_array_tag, codec::typed_array>(source)(actual);
            decoder(std::span{encoded}.first(cut));
            if (!decoder.done()) {
                decoder(std::span{encoded}.subspan(cut), end_of_input);
            }
            REQUIRE(decoder.done());
            REQUIRE_FALSE(decoder.result());
            CHECK(decoder.result().error() == status_code::size_limit_exceeded);
            CHECK(actual.values() == std::vector<std::uint16_t>{99});
            CHECK(source.committed_position().offset() == 0);
        }

        const array_type                      expected{0x1234, 0xabcd};
        std::array<std::byte, encoded.size()> output;
        output_chunks                         chunks{std::span{output}};
        auto                                  sink = make_output_source(chunks);
        auto encoder                               = make_resumable_encoder<rejecting_typed_array_tag, codec::typed_array>(sink)(expected);
        REQUIRE(encoder.done());
        REQUIRE_FALSE(encoder.result());
        CHECK(encoder.result().error() == status_code::size_limit_exceeded);
        CHECK(std::ranges::empty(encoder.produced()));
        CHECK(sink.committed_position().offset() == 0);
    }

    TEST_CASE("truncated payload commits only complete elements and preserves unfinished input") {
        for (std::size_t cut = 0; cut != encoded.size(); ++cut) {
            CAPTURE(cut);
            input_chunks chunks;
            auto         source = make_input_source(chunks);
            array_type   actual{99};
            auto         operation = make_resumable_decoder<codec::typed_array>(source)(actual);
            operation(std::span{encoded}.first(cut), end_of_input);
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::incomplete);

            if (cut < 3) {
                CHECK(actual.values() == std::vector<std::uint16_t>{99});
                CHECK(source.committed_position().offset() == 0);
                CHECK(std::ranges::distance(operation.remaining_input()) == static_cast<std::ptrdiff_t>(cut));
            } else {
                const auto complete = (cut - 3) / 2;
                REQUIRE(actual.values().size() == complete);
                if (complete != 0) {
                    CHECK(actual.values().front() == 0x1234);
                }
                CHECK(source.committed_position().offset() == 3 + complete * 2);
                CHECK(std::ranges::distance(operation.remaining_input()) == static_cast<std::ptrdiff_t>((cut - 3) % 2));
            }
        }
    }

    TEST_CASE("header admission reserves storage before payload arrival and empty payload clears the destination") {
        const std::array header{std::byte{0xd8}, std::byte{0x45}, std::byte{0x59}, std::byte{0x08}, std::byte{0x00}};
        input_chunks     chunks;
        auto             source = make_input_source(chunks);
        array_type       actual{99};
        auto             operation = make_resumable_decoder<codec::typed_array>(source)(actual);
        operation(std::span{header});
        CHECK_FALSE(operation.done());
        CHECK(actual.values().empty());
        CHECK(actual.values().capacity() >= 1024);
        CHECK(source.committed_position().offset() == header.size());
        operation.cancel();

        const std::array empty{std::byte{0xd8}, std::byte{0x45}, std::byte{0x40}};
        input_chunks     empty_chunks;
        auto             empty_source = make_input_source(empty_chunks);
        array_type       empty_actual{99};
        auto             empty_operation = make_resumable_decoder<codec::typed_array>(empty_source)(empty_actual);
        empty_operation(std::span{empty});
        REQUIRE(empty_operation.done());
        REQUIRE(empty_operation.result());
        CHECK(*empty_operation.result() == empty.size());
        CHECK(empty_actual.values().empty());
    }

    TEST_CASE("impossible destination reservation reports allocation failure without payload bytes") {
        // This aligned element count exceeds vector::max_size without allocating memory.
        const std::array header{std::byte{0xd8}, std::byte{0x45}, std::byte{0x5b}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
                                std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xfe}};
        input_chunks     chunks;
        auto             source = make_input_source(chunks);
        array_type       actual;
        auto             operation = make_resumable_decoder<codec::typed_array>(source)(actual);
        operation(std::span{header});
        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
            CHECK(operation.result().error() == status_code::size_limit_exceeded);
        } else {
            CHECK(operation.result().error() == status_code::out_of_memory);
            CHECK(source.committed_position().offset() == header.size());
        }
        CHECK(actual.values().empty());
    }

    TEST_CASE("typed array leaves following items available and reports the consumed size") {
        std::vector<std::byte> bytes(encoded.begin(), encoded.end());
        bytes.push_back(std::byte{0x18});
        bytes.push_back(std::byte{0x2a});
        input_chunks chunks;
        auto         source = make_input_source(chunks);
        array_type   actual;
        {
            auto operation = make_resumable_decoder<codec::typed_array>(source)(actual);
            operation(std::span<const std::byte>{bytes}, end_of_input);
            REQUIRE(operation.result());
            CHECK(*operation.result() == encoded.size());
            CHECK(actual.values() == std::vector<std::uint16_t>{0x1234, 0xabcd});
            CHECK(std::ranges::distance(operation.remaining_input()) == 2);
        }

        unsigned value{};
        auto     following = make_resumable_decoder(source)(value);
        REQUIRE(following.done());
        REQUIRE(following.result());
        CHECK(*following.result() == 2);
        CHECK(value == 42);
    }

    TEST_CASE("encoding preserves exact wire bytes and commits only complete elements at eof") {
        const array_type expected{0x1234, 0xabcd};
        for (std::size_t size = 0; size <= encoded.size(); ++size) {
            CAPTURE(size);
            std::vector<std::byte> bytes(size);
            output_chunks          chunks;
            auto                   sink      = make_output_source(chunks);
            auto                   operation = make_resumable_encoder<codec::typed_array>(sink)(expected);
            operation(std::span{bytes}, end_of_input);
            REQUIRE(operation.done());
            if (size == encoded.size()) {
                REQUIRE(operation.result());
                CHECK(*operation.result() == encoded.size());
                CHECK(std::ranges::equal(bytes, encoded));
            } else {
                REQUIRE_FALSE(operation.result());
                CHECK(operation.result().error() == status_code::incomplete);
                const auto committed = size < 3 ? 0 : 3 + ((size - 3) / 2) * 2;
                CHECK(sink.committed_position().offset() == committed);
                CHECK(std::ranges::distance(operation.produced()) == static_cast<std::ptrdiff_t>(committed));
                CHECK(std::ranges::equal(operation.produced(), std::span{encoded}.first(committed)));
            }
        }
    }
}
