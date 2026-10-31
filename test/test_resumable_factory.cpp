#include "test_util.h"

#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cbor_tags/codec/typed_array.h>
#include <cstddef>
#include <cstdint>
#include <list>
#include <ranges>
#include <span>
#include <string>
#include <tuple>
#include <vector>

using namespace cbor::tags;

namespace {
using input_chunks  = std::list<std::span<const std::byte>>;
using output_chunks = std::list<std::span<std::byte>>;

struct typed_record {
    std::string                         name;
    rfc8746::typed_array<std::uint32_t> values;
};

// An ordinary customization commits only when its entire item succeeds.
struct ordinary_text {
    std::string                      value;
    template <typename Encoder> auto encode(Encoder &encoder) const { return encoder(value); }
};
} // namespace

TEST_SUITE("roundtrip/resumable_factory") {
    TEST_CASE("named decoder accepts empty batches and vector descriptor growth") {
        const std::vector<std::string> expected{"first", std::string(256, 'x'), "last"};
        std::vector<std::byte>         encoded;
        REQUIRE(make_encoder(encoded)(expected));
        const auto bytes = std::span<const std::byte>{encoded};

        std::vector<std::span<const std::byte>> chunks;
        chunks.reserve(1);
        auto                     decoder = make_resumable_decoder(chunks);
        std::vector<std::string> actual;
        auto                     operation = decoder(actual);
        CHECK(operation(bytes.first(0)) == resume_state::need_input);
        CHECK(chunks.empty());

        const std::array batch{bytes.first(0), bytes.first(1), bytes.subspan(1, 1)};
        CHECK(operation(std::span{batch}) == resume_state::need_input);
        CHECK(chunks.size() == 2);
        for (std::size_t offset = 2; offset < bytes.size(); ++offset) {
            operation(bytes.subspan(offset, 1));
        }

        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(*operation.result() == bytes.size());
        CHECK(actual == expected);
        CHECK(chunks.size() == bytes.size());
        CHECK(operation.remaining_input().empty());
    }

    TEST_CASE("named decoder continues through prepopulated fixed descriptor storage") {
        const std::uint32_t    first  = 24;
        const std::string      second = "next item";
        std::vector<std::byte> encoded;
        auto                   ordinary = make_encoder(encoded);
        REQUIRE(ordinary(first));
        const auto first_size = encoded.size();
        REQUIRE(ordinary(second));
        const auto bytes = std::span<const std::byte>{encoded};
        std::array descriptors{bytes.first(first_size), bytes.subspan(first_size)};
        auto       storage = std::span{descriptors};
        auto       decoder = make_resumable_decoder(storage);

        {
            std::uint32_t actual{};
            auto          operation = decoder(actual);
            REQUIRE(operation.done());
            REQUIRE(operation.result());
            CHECK(actual == first);
            CHECK(*operation.result() == first_size);
            CHECK(std::ranges::distance(operation.remaining_input()) == static_cast<std::ptrdiff_t>(bytes.size() - first_size));

            // Completion alone does not release the shared cursor.
            std::string other = "untouched";
            CHECK_THROWS_AS(decoder(other), std::logic_error);
            CHECK(other == "untouched");
        }

        std::string actual;
        auto        operation = decoder(actual);
        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(actual == second);
        CHECK(*operation.result() == bytes.size() - first_size);
        CHECK(operation.remaining_input().empty());
    }

    TEST_CASE("named encoder preserves per-operation output views across sequential calls") {
        const std::uint32_t        first  = 65536;
        const std::string          second = "second";
        std::array<std::byte, 128> bytes{};
        std::array                 descriptors{std::span{bytes}.first(2), std::span{bytes}.subspan(2)};
        auto                       storage = std::span{descriptors};
        auto                       encoder = make_resumable_encoder(storage);

        auto first_view = [&] {
            auto operation = encoder(first);
            REQUIRE(operation.done());
            REQUIRE(operation.result());
            CHECK_THROWS_AS(encoder(second), std::logic_error);
            return operation.produced();
        }();

        auto operation = encoder(second);
        REQUIRE(operation.done());
        REQUIRE(operation.result());
        auto second_view = operation.produced();
        CHECK(first_view.end() == second_view.begin());
        CHECK(static_cast<std::size_t>(std::ranges::distance(first_view)) + *operation.result() +
                  static_cast<std::size_t>(std::ranges::distance(operation.remaining_output())) ==
              bytes.size());

        // The views still borrow the named factory, even after the first operation died.
        std::uint32_t first_actual{};
        std::string   second_actual;
        REQUIRE(make_decoder(first_view)(first_actual));
        REQUIRE(make_decoder(second_view)(second_actual));
        CHECK(first_actual == first);
        CHECK(second_actual == second);
    }

    TEST_CASE("named encoder excludes tentative ordinary writes and unused capacity") {
        const ordinary_text        expected{"ordinary item spanning output buffers"};
        std::array<std::byte, 2>   first{};
        std::array<std::byte, 128> second{};
        output_chunks              chunks;
        auto                       encoder   = make_resumable_encoder(chunks);
        auto                       operation = encoder(expected);

        CHECK(operation(first) == resume_state::need_output);
        CHECK(operation.produced().empty());
        CHECK(operation(std::span<std::byte>{}) == resume_state::need_output);
        CHECK(chunks.size() == 1);
        REQUIRE(operation(second) == resume_state::done);
        REQUIRE(operation.result());
        auto        produced = operation.produced();
        std::string actual;
        auto        decoder = make_decoder(produced);
        REQUIRE(decoder(actual));
        CHECK(actual == expected.value);
        CHECK(decoder.tell() == produced.end());
        CHECK(*operation.result() < first.size() + second.size());
    }

    TEST_CASE("named factories forward group options and typed array codecs") {
        using unwrapped_options = Options<default_expected>;
        const typed_record     expected{"typed", rfc8746::typed_array<std::uint32_t>{std::vector<std::uint32_t>{1, 24, 65536}}};
        std::vector<std::byte> reference;
        REQUIRE(make_encoder_with_options<unwrapped_options, codec::typed_array>(reference)(expected));

        // Independently decode factory output using the ordinary codec and options.
        std::vector<std::byte> bytes(reference.size());
        output_chunks          outputs;
        auto                   encoder  = make_resumable_encoder_with_options<unwrapped_options, codec::typed_array>(outputs);
        auto                   encoding = encoder(expected);
        for (auto &byte : bytes) {
            encoding(std::span{&byte, 1});
        }
        REQUIRE(encoding.done());
        REQUIRE(encoding.result());
        CHECK(*encoding.result() == reference.size());
        typed_record decoded;
        REQUIRE(make_decoder_with_options<unwrapped_options, codec::typed_array>(bytes)(decoded));
        CHECK(decoded.name == expected.name);
        CHECK(decoded.values.values() == expected.values.values());

        // Decode independently produced ordinary bytes through the named factory.
        input_chunks inputs;
        auto         decoder = make_resumable_decoder_with_options<unwrapped_options, codec::typed_array>(inputs);
        typed_record actual;
        auto         decoding = decoder(actual);
        for (const auto &byte : reference) {
            decoding(std::span{&byte, 1});
        }
        REQUIRE(decoding.done());
        REQUIRE(decoding.result());
        CHECK(*decoding.result() == reference.size());
        CHECK(actual.name == expected.name);
        CHECK(actual.values.values() == expected.values.values());
    }

    TEST_CASE("named factory finish makes a truncated typed value terminal") {
        const std::string      expected = "incomplete text";
        std::vector<std::byte> bytes;
        REQUIRE(make_encoder(bytes)(expected));
        input_chunks chunks;
        auto         decoder = make_resumable_decoder(chunks);
        std::string  actual;
        auto         operation = decoder(actual);
        REQUIRE(operation(std::span<const std::byte>{bytes}.first(bytes.size() - 1)) == resume_state::need_input);
        CHECK_THROWS_AS(decoder(actual), std::logic_error);
        CHECK(operation.finish() == resume_state::done);
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::incomplete);
        CHECK(actual == expected.substr(0, expected.size() - 1));
    }
}
