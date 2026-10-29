#include <cbor_tags/cbor_resumable.h>
#include <doctest/doctest.h>
#include <iterator>
#include <list>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

using namespace cbor::tags;

namespace {

using input_chunks  = std::list<std::span<const std::byte>>;
using output_chunks = std::list<std::span<std::byte>>;

struct ready_record {
    std::string text;
    float       value{};
    bool        operator==(const ready_record &) const = default;
};

struct tagged_ready_record {
    static constexpr std::uint64_t cbor_tag = 1000;
    std::string                    text;
    bool                           operator==(const tagged_ready_record &) const = default;
};

using ready_variant = std::variant<int, std::string, tagged_ready_record>;

struct observed_byte_iterator {
    using value_type       = std::byte;
    using difference_type  = std::ptrdiff_t;
    using iterator_concept = std::input_iterator_tag;
    std::list<std::byte>::const_iterator current;
    std::size_t                         *reads{};

    std::byte operator*() const {
        ++*reads;
        return *current;
    }
    observed_byte_iterator &operator++() {
        ++current;
        return *this;
    }
    void operator++(int) { ++*this; }
    bool operator==(const observed_byte_iterator &) const = default;
};

template <typename T> void check_ready_and_missing(const T &expected) {
    std::vector<std::byte> wire;
    REQUIRE(make_encoder(wire)(expected));
    for (std::size_t cut = 0; cut <= wire.size(); ++cut) {
        CAPTURE(cut);
        const std::span<const std::byte> input{wire};
        input_chunks                     storage{input.first(cut)};
        auto                             source = make_input_source(storage);
        T                                actual;
        auto                             decoder = make_resumable_decoder(source)(actual);
        CHECK(decoder.done() == (cut == wire.size()));
        if (!decoder.done())
            decoder(input.subspan(cut));
        REQUIRE(decoder.done());
        REQUIRE(decoder.result());
        CHECK(actual == expected);
        CHECK(*decoder.result() == wire.size());

        std::vector<std::byte> output(wire.size());
        output_chunks          chunks{std::span{output}.first(cut)};
        auto                   sink = make_output_source(chunks);
        // The temporary belongs to arguments(Tuple values), including any
        // nested temporary tuples forwarded through metadata-only entrypoints.
        auto encoder = make_resumable_encoder(sink)(T{expected});
        CHECK(encoder.done() == (cut == wire.size()));
        if (!encoder.done())
            encoder(std::span{output}.subspan(cut));
        REQUIRE(encoder.done());
        REQUIRE(encoder.result());
        T independently_decoded;
        REQUIRE(make_decoder(output)(independently_decoded));
        CHECK(independently_decoded == expected);
    }
}

} // namespace

TEST_SUITE("roundtrip/resumable") {

    TEST_CASE("ready metadata forwarding keeps temporary tuples and aggregates alive while suspended") {
        check_ready_and_missing(ready_record{"owned aggregate text", 0.125F});
        check_ready_and_missing(std::tuple{std::string{"owned tuple text"}, 0.125F});
        check_ready_and_missing(std::optional<ready_record>{ready_record{"optional group", 32.5F}});
        check_ready_and_missing(std::optional<ready_record>{});
    }

    TEST_CASE("variant lookahead and alternative selection retain their selected local across splits") {
        const ready_variant    expected{tagged_ready_record{"selected alternative remains alive"}};
        std::vector<std::byte> wire;
        REQUIRE(make_encoder(wire)(expected));
        for (std::size_t cut = 0; cut <= wire.size(); ++cut) {
            CAPTURE(cut);
            input_chunks  storage{std::span<const std::byte>{wire}.first(cut)};
            auto          source = make_input_source(storage);
            ready_variant actual{17};
            auto          operation = make_resumable_decoder(source)(actual);
            if (!operation.done()) {
                REQUIRE(std::holds_alternative<int>(actual));
                CHECK(std::get<int>(actual) == 17);
                operation(std::span<const std::byte>{wire}.subspan(cut));
            }
            REQUIRE(operation.done());
            REQUIRE(operation.result());
            CHECK(actual == expected);
            CHECK(*operation.result() == wire.size());
        }
    }

    TEST_CASE("ready payload entrypoints return an inline result for contiguous and noncontiguous ranges") {
        const std::vector<std::byte> bytes(32, std::byte{0x5a});
        const auto                   check = [&](auto &value) {
            std::vector<std::byte>                                            output(bytes.size());
            output_chunks                                                     storage{std::span{output}};
            auto                                                              sink = make_output_source(storage);
            detail::resume_control                                            control;
            detail::resumable_encode_context<decltype(sink), default_options> context{sink, control};
            auto                                                              result = context.payload(value);
            REQUIRE(result.done());
            CHECK_FALSE(result.handle());
            CHECK(result.result() == status_code::success);
            CHECK(context.offset() == bytes.size());
            CHECK(output == bytes);
        };
        check(bytes);
        const std::list<std::byte> noncontiguous(bytes.begin(), bytes.end());
        check(noncontiguous);
    }

    TEST_CASE("payload continuations observe each single pass byte once across empty output windows") {
        const std::list<std::byte> bytes{std::byte{0x13}, std::byte{0x29}, std::byte{0x57}};
        std::size_t                reads{};
        auto range = std::ranges::subrange{observed_byte_iterator{bytes.begin(), &reads}, observed_byte_iterator{bytes.end(), &reads}};
        static_assert(std::ranges::input_range<decltype(range)>);
        static_assert(!std::ranges::forward_range<decltype(range)>);
        std::vector<std::byte>                                            output(bytes.size());
        output_chunks                                                     storage;
        auto                                                              sink = make_output_source(storage);
        detail::resume_control                                            control;
        detail::resumable_encode_context<decltype(sink), default_options> context{sink, control};
        auto                                                              result = context.payload(range);
        REQUIRE_FALSE(result.done());
        result.handle().resume();
        REQUIRE(control.leaf);
        std::exchange(control.leaf, {}).resume();
        CHECK(reads == 1U);
        for (auto &byte : output) {
            sink.append(std::span{&byte, 1});
            REQUIRE(control.leaf);
            std::exchange(control.leaf, {}).resume();
        }
        REQUIRE(result.done());
        CHECK(result.result() == status_code::success);
        CHECK(reads == bytes.size());
        CHECK(std::ranges::equal(output, bytes));
    }

    TEST_CASE("optional and variant availability finish incomplete without changing their values") {
        input_chunks                storage;
        auto                        source = make_input_source(storage);
        std::optional<ready_record> optional{ready_record{"retained", 1.25F}};
        {
            auto operation = make_resumable_decoder(source)(optional);
            REQUIRE_FALSE(operation.done());
            operation.finish();
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::incomplete);
            CHECK(optional == std::optional<ready_record>{ready_record{"retained", 1.25F}});
        }
        ready_variant variant{17};
        auto          operation = make_resumable_decoder(source)(variant);
        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::incomplete);
        CHECK(variant == ready_variant{17});
    }
}
