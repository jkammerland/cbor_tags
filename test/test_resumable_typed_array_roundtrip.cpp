#include <algorithm>
#include <array>
#include <bit>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cbor_tags/codec/typed_array.h>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <iterator>
#include <list>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

using namespace cbor::tags;

namespace {

using input_chunks  = std::list<std::span<const std::byte>>;
using output_chunks = std::list<std::span<std::byte>>;

// Observe physical traversal independently of the source's logical cursor.
struct counted_typed_array_iterator {
    using value_type        = std::byte;
    using difference_type   = std::ptrdiff_t;
    using iterator_category = std::bidirectional_iterator_tag;
    using iterator_concept  = std::bidirectional_iterator_tag;

    std::list<std::byte>::const_iterator position;
    std::size_t                         *increments{};

    const std::byte              &operator*() const { return *position; }
    counted_typed_array_iterator &operator++() {
        ++*increments;
        ++position;
        return *this;
    }
    counted_typed_array_iterator operator++(int) {
        const auto previous = *this;
        ++*this;
        return previous;
    }
    counted_typed_array_iterator &operator--() {
        --position;
        return *this;
    }
    counted_typed_array_iterator operator--(int) {
        const auto previous = *this;
        --*this;
        return previous;
    }
    friend bool operator==(const counted_typed_array_iterator &left, const counted_typed_array_iterator &right) {
        return left.position == right.position;
    }
};

using counted_input_chunk = std::ranges::subrange<counted_typed_array_iterator>;
static_assert(std::bidirectional_iterator<counted_typed_array_iterator>);
static_assert(!std::ranges::sized_range<counted_input_chunk>);

template <typename Array> std::vector<std::byte> typed_array_bytes(const Array &array) {
    std::vector<std::byte> bytes;
    REQUIRE(make_encoder<codec::typed_array>(bytes)(array));
    return bytes;
}

// Comparing representations also verifies that floating-point NaN payloads survive.
template <typename Array> void check_typed_array_values(const Array &actual, const Array &expected) {
    REQUIRE(actual.values().size() == expected.values().size());
    CHECK(std::ranges::equal(std::as_bytes(actual.span()), std::as_bytes(expected.span())));
}

template <typename Array, typename EncodeValue> void check_typed_array_encode_splits(const Array &expected, const EncodeValue &value) {
    const auto size = typed_array_bytes(expected).size();
    for (std::size_t cut = 0; cut <= size; ++cut) {
        CAPTURE(cut);
        std::vector<std::byte> bytes(size);
        output_chunks          chunks;
        auto                   sink      = make_output_source(chunks);
        auto                   operation = make_resumable_encoder<codec::typed_array>(sink)(value);
        operation(std::span{bytes}.first(cut));
        if (!operation.done()) {
            operation(std::span{bytes}.subspan(cut), end_of_input);
        }

        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(*operation.result() == size);
        CHECK(std::ranges::distance(operation.produced()) == static_cast<std::ptrdiff_t>(size));
        CHECK(std::ranges::empty(operation.remaining_output()));

        Array actual;
        REQUIRE(make_decoder<codec::typed_array>(bytes)(actual));
        check_typed_array_values(actual, expected);
    }
}

template <typename Array> void check_typed_array_splits(const Array &expected) {
    const auto bytes = typed_array_bytes(expected);
    const auto input = std::span<const std::byte>{bytes};
    for (std::size_t cut = 0; cut <= bytes.size(); ++cut) {
        CAPTURE(cut);
        input_chunks chunks;
        auto         source = make_input_source(chunks);
        Array        actual;
        auto         operation = make_resumable_decoder<codec::typed_array>(source)(actual);
        operation(input.first(cut));
        if (!operation.done()) {
            operation(input.subspan(cut), end_of_input);
        }

        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(*operation.result() == bytes.size());
        CHECK(std::ranges::empty(operation.remaining_input()));
        check_typed_array_values(actual, expected);
    }
    check_typed_array_encode_splits(expected, expected);
}

template <typename T> void check_typed_array_byte_orders(std::vector<T> values) {
    check_typed_array_splits(rfc8746::typed_array<T, rfc8746::typed_array_byte_order::little>{values});
    check_typed_array_splits(rfc8746::typed_array<T, rfc8746::typed_array_byte_order::big>{std::move(values)});
}

// An explicit codec hook remains the selected customization before this codec.
template <typename Self> struct typed_array_override : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;

    resumable<status_code> resumable_decode(rfc8746::typed_array<std::uint32_t> &array) {
        std::uint32_t value{};
        const auto    status = co_await static_cast<Self &>(*this)(value);
        if (status == status_code::success) {
            array.values() = {value};
        }
        co_return status;
    }

    resumable<status_code> resumable_encode(const rfc8746::typed_array<std::uint32_t> &array) {
        co_return co_await static_cast<Self &>(*this)(array.values().front());
    }
};

} // namespace

TEST_SUITE("roundtrip/resumable_typed_array") {
    TEST_CASE("owning typed arrays preserve integer and floating representations at every transport split") {
        check_typed_array_splits(rfc8746::typed_array<std::uint8_t>{0, 1, 127, 255});
        check_typed_array_splits(rfc8746::typed_array<std::int8_t>{-128, -1, 0, 127});
        check_typed_array_splits(rfc8746::typed_array<rfc8746::uint8_clamped>{rfc8746::uint8_clamped{0}, rfc8746::uint8_clamped{255}});
        check_typed_array_byte_orders<std::uint16_t>({0, 1, 0x1234, 0xffff});
        check_typed_array_byte_orders<std::uint32_t>({0, 0x12345678, 0xffffffff});
        check_typed_array_byte_orders<std::uint64_t>({0, 0x123456789abcdef0, 0xffffffffffffffff});
        check_typed_array_byte_orders<std::int16_t>({-32768, -1, 0, 32767});
        check_typed_array_byte_orders<std::int32_t>({-2147483647 - 1, -1, 0, 2147483647});
        check_typed_array_byte_orders<std::int64_t>({-9223372036854775807LL - 1, -1, 0, 9223372036854775807LL});
        check_typed_array_byte_orders<float16_t>(
            {std::bit_cast<float16_t>(std::uint16_t{0x7e01}), std::bit_cast<float16_t>(std::uint16_t{0x8000})});
        check_typed_array_byte_orders<float>({std::bit_cast<float>(std::uint32_t{0x7fc12345}), -0.0F, 1.25F});
        check_typed_array_byte_orders<double>({std::bit_cast<double>(std::uint64_t{0x7ff8123456789abc}), -0.0, 1.25});

        rfc8746::float128_t wide;
        for (std::size_t index = 0; index != wide.bytes.size(); ++index) {
            wide.bytes[index] = static_cast<std::byte>(index * 13);
        }
        check_typed_array_byte_orders<rfc8746::float128_t>({wide, {}});
    }

    TEST_CASE("empty arrays and longer length headers survive every split") {
        check_typed_array_splits(rfc8746::typed_array<std::uint32_t>{});
        check_typed_array_splits(rfc8746::typed_array<std::uint32_t>{std::vector<std::uint32_t>(70, 123456)});
    }

    TEST_CASE("borrowed contiguous and noncontiguous ranges encode through resumable hooks") {
        using order = rfc8746::typed_array_byte_order;
        const std::vector<std::uint32_t>                      values{1, 123456, 0xffffffff};
        const rfc8746::typed_array<std::uint32_t, order::big> expected{values};
        const auto                                            annotated = rfc8746::as_typed_array_be(values);
        check_typed_array_encode_splits(expected, annotated);

        const rfc8746::typed_array_ref<std::uint32_t, order::big> reference{std::span{values}};
        check_typed_array_encode_splits(expected, reference);

        std::list<std::uint32_t> linked(values.begin(), values.end());
        const auto               annotated_linked = rfc8746::as_typed_array_be(linked);
        check_typed_array_encode_splits(expected, annotated_linked);
    }

    TEST_CASE("temporary typed array wrappers retain their descriptors across suspension") {
        std::vector<std::uint32_t>                values{1, 123456, 0xffffffff};
        const rfc8746::typed_array<std::uint32_t> expected{values};
        const auto                                size = typed_array_bytes(expected).size();

        // Exercise both a descriptor owned by the temporary wrapper and a binding
        // to a caller-owned container; the payload remains alive in either case.
        auto check_output = [&](auto make_operation) {
            std::vector<std::byte> output(size);
            output_chunks          chunks;
            auto                   sink      = make_output_source(chunks);
            auto                   operation = make_operation(sink);
            REQUIRE_FALSE(operation.done());
            for (auto &byte : output) {
                operation(std::span{&byte, 1});
            }
            REQUIRE(operation.done());
            REQUIRE(operation.result());
            CHECK(*operation.result() == output.size());

            rfc8746::typed_array<std::uint32_t> actual;
            REQUIRE(make_decoder<codec::typed_array>(output)(actual));
            check_typed_array_values(actual, expected);
        };

        check_output([&](auto &sink) { return make_resumable_encoder<codec::typed_array>(sink)(rfc8746::as_typed_array(values)); });
        check_output(
            [&](auto &sink) { return make_resumable_encoder<codec::typed_array>(sink)(rfc8746::as_typed_array(std::as_const(values))); });
        check_output(
            [&](auto &sink) { return make_resumable_encoder<codec::typed_array>(sink)(rfc8746::as_typed_array(std::span{values})); });
    }

    TEST_CASE("unsized noncontiguous transport chunks preserve split elements") {
        using array_type = rfc8746::typed_array<std::uint32_t, rfc8746::typed_array_byte_order::big>;
        const array_type expected{1, 123456, 0xffffffff};
        const auto       bytes = typed_array_bytes(expected);

        std::list<std::byte> linked(bytes.begin(), bytes.end());
        using input_chunk = std::ranges::subrange<std::list<std::byte>::const_iterator>;
        static_assert(!std::ranges::sized_range<input_chunk>);
        std::list<input_chunk> input_storage;
        auto                   source = make_input_source(input_storage);
        array_type             actual;
        auto                   decoder = make_resumable_decoder<codec::typed_array>(source)(actual);
        for (auto iterator = linked.cbegin(); iterator != linked.cend(); ++iterator) {
            decoder(input_chunk{iterator, std::next(iterator)});
        }
        REQUIRE(decoder.done());
        REQUIRE(decoder.result());
        CHECK(*decoder.result() == bytes.size());
        check_typed_array_values(actual, expected);

        std::list<std::byte> output(bytes.size());
        using output_chunk = std::ranges::subrange<std::list<std::byte>::iterator>;
        static_assert(!std::ranges::sized_range<output_chunk>);
        std::list<output_chunk> output_storage;
        auto                    sink    = make_output_source(output_storage);
        auto                    encoder = make_resumable_encoder<codec::typed_array>(sink)(expected);
        for (auto iterator = output.begin(); iterator != output.end(); ++iterator) {
            encoder(output_chunk{iterator, std::next(iterator)});
        }
        REQUIRE(encoder.done());
        REQUIRE(encoder.result());
        CHECK(*encoder.result() == bytes.size());

        array_type decoded_output;
        REQUIRE(make_decoder<codec::typed_array>(output)(decoded_output));
        check_typed_array_values(decoded_output, expected);
    }

    TEST_CASE("unsized typed array payload traverses each admitted byte exactly once") {
        using array_type = rfc8746::typed_array<std::uint32_t, rfc8746::typed_array_byte_order::big>;
        const array_type expected{1, 123456, 0xffffffff};
        const auto       bytes = typed_array_bytes(expected);

        as_tag_any  tag;
        as_bstr_any bstr;
        auto        header_decoder = make_decoder(bytes);
        REQUIRE(header_decoder(tag, bstr));
        REQUIRE(bstr.size == expected.span().size_bytes());
        std::list<std::byte> header(bytes.cbegin(), header_decoder.tell());
        std::list<std::byte> payload(header_decoder.tell(), bytes.cend());
        std::size_t          increments{};

        std::list<counted_input_chunk> chunks;
        auto                           source = make_input_source(chunks);
        static_assert(!std::ranges::sized_range<decltype(source)>);
        array_type actual;
        auto       operation = make_resumable_decoder<codec::typed_array>(source)(actual);
        operation(counted_input_chunk{counted_typed_array_iterator{header.cbegin(), &increments},
                                      counted_typed_array_iterator{header.cend(), &increments}});
        REQUIRE_FALSE(operation.done());
        CHECK(actual.values().empty());
        CHECK(actual.values().capacity() >= expected.values().size());
        CHECK(increments == header.size());

        // A complete admitted payload catches both prewalks and repeated decoding.
        operation(counted_input_chunk{counted_typed_array_iterator{payload.cbegin(), &increments},
                                      counted_typed_array_iterator{payload.cend(), &increments}});
        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(*operation.result() == bytes.size());
        CHECK(increments == bytes.size());
        check_typed_array_values(actual, expected);
    }

    TEST_CASE("wide elements remain resumable across repeated single byte windows") {
        using array_type = rfc8746::typed_array<rfc8746::float128_t, rfc8746::typed_array_byte_order::big>;
        rfc8746::float128_t element;
        for (std::size_t index = 0; index != element.bytes.size(); ++index) {
            element.bytes[index] = static_cast<std::byte>(index * 7);
        }
        const array_type expected{element, element};
        const auto       reference = typed_array_bytes(expected);

        input_chunks chunks;
        auto         source = make_input_source(chunks);
        array_type   actual;
        auto         decoder = make_resumable_decoder<codec::typed_array>(source)(actual);
        for (const auto &byte : reference) {
            decoder(std::span{&byte, 1});
        }
        REQUIRE(decoder.result());
        CHECK(*decoder.result() == reference.size());
        check_typed_array_values(actual, expected);

        std::vector<std::byte> output(reference.size());
        output_chunks          output_storage;
        auto                   sink    = make_output_source(output_storage);
        auto                   encoder = make_resumable_encoder<codec::typed_array>(sink)(expected);
        for (auto &byte : output) {
            encoder(std::span{&byte, 1});
        }
        REQUIRE(encoder.result());
        CHECK(*encoder.result() == output.size());
        array_type decoded;
        REQUIRE(make_decoder<codec::typed_array>(output)(decoded));
        check_typed_array_values(decoded, expected);
    }

    TEST_CASE("an earlier explicit codec customization retains dispatch precedence") {
        const rfc8746::typed_array<std::uint32_t> expected{123456};
        std::vector<std::byte>                    reference;
        REQUIRE(make_encoder(reference)(expected.values().front()));

        std::vector<std::byte> output(reference.size());
        output_chunks          output_storage;
        auto                   sink    = make_output_source(output_storage);
        auto                   encoder = make_resumable_encoder<typed_array_override, codec::typed_array>(sink)(expected);
        encoder(std::span{output}, end_of_input);
        REQUIRE(encoder.result());
        CHECK(*encoder.result() == reference.size());

        input_chunks                        input_storage;
        auto                                source = make_input_source(input_storage);
        rfc8746::typed_array<std::uint32_t> actual;
        auto                                decoder = make_resumable_decoder<typed_array_override, codec::typed_array>(source)(actual);
        decoder(std::span<const std::byte>{output}, end_of_input);
        REQUIRE(decoder.result());
        CHECK(*decoder.result() == reference.size());
        check_typed_array_values(actual, expected);
    }
}
