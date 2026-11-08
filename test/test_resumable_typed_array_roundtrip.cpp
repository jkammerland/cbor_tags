#include <algorithm>
#include <array>
#include <bit>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cbor_tags/codec/typed_array.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <forward_list>
#include <iterator>
#include <list>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

using namespace cbor::tags;

namespace {

// A typed-array trait can describe a value without a default constructor.
struct non_default_typed_word {
    std::uint32_t value;

    non_default_typed_word() = delete;
    explicit constexpr non_default_typed_word(std::uint32_t initial) noexcept : value(initial) {}
};

static_assert(std::is_trivially_copyable_v<non_default_typed_word>);
static_assert(!std::is_default_constructible_v<non_default_typed_word>);

} // namespace

namespace cbor::tags::rfc8746 {

template <typed_array_byte_order Order>
struct typed_array_traits<non_default_typed_word, Order> : typed_array_traits<std::uint32_t, Order> {};

} // namespace cbor::tags::rfc8746

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
        auto                   sink      = make_output_buffer(chunks);
        auto                   operation = make_resumable_encoder<codec::typed_array>(sink)(value);
        chunks.emplace_back(std::span{bytes}.first(cut));
        operation.resume();
        if (!operation.done()) {
            chunks.emplace_back(std::span{bytes}.subspan(cut));
            operation.close_output();
        }

        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(*operation.result() == size);
        CHECK(static_cast<std::ptrdiff_t>(operation.committed_bytes()) == static_cast<std::ptrdiff_t>(size));
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
        chunks.emplace_back(input.first(cut));
        operation.resume();
        if (!operation.done()) {
            chunks.emplace_back(input.subspan(cut));
            operation.close_input();
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

template <rfc8746::typed_array_byte_order Order, typename R> auto as_destination(R &&range) {
    if constexpr (Order == rfc8746::typed_array_byte_order::big) {
        return rfc8746::as_typed_array_be(std::forward<R>(range));
    } else {
        return rfc8746::as_typed_array(std::forward<R>(range));
    }
}

template <typename Storage> const auto &stored_values(const Storage &storage) {
    if constexpr (requires { storage.values(); }) {
        return storage.values();
    } else {
        return storage;
    }
}

// Decodes into a copy of initial at every split. make_value wraps the copy in
// whatever gets decoded, like as_typed_array or a bound.
template <rfc8746::typed_array_byte_order Order, typename Storage, typename MakeValue>
void check_destination_splits(const std::vector<std::uint32_t> &expected, const Storage &initial, MakeValue make_value) {
    const auto bytes = typed_array_bytes(rfc8746::typed_array<std::uint32_t, Order>{expected});
    const auto input = std::span<const std::byte>{bytes};
    for (std::size_t cut = 0; cut <= bytes.size(); ++cut) {
        CAPTURE(cut);
        input_chunks chunks;
        auto         source    = make_input_source(chunks);
        Storage      storage   = initial;
        auto         operation = make_resumable_decoder<codec::typed_array>(source)(make_value(storage));
        chunks.emplace_back(input.first(cut));
        operation.resume();
        if (!operation.done()) {
            chunks.emplace_back(input.subspan(cut));
            operation.close_input();
        }

        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(*operation.result() == bytes.size());
        CHECK(std::ranges::empty(operation.remaining_input()));
        CHECK(std::ranges::equal(stored_values(storage), expected));
    }
}

template <rfc8746::typed_array_byte_order Order, typename Decoder>
using view_for = std::conditional_t<Order == rfc8746::typed_array_byte_order::big, rfc8746::typed_array_view_be_for<std::uint32_t, Decoder>,
                                    rfc8746::typed_array_view_for<std::uint32_t, Decoder>>;

// Decodes a view at every split, through a source and through bound storage.
// A view commits nothing until its whole item is there, then outlives its operation.
template <rfc8746::typed_array_byte_order Order, typename MakeValue>
void check_view_splits(const std::vector<std::uint32_t> &expected, MakeValue make_value) {
    const auto bytes = typed_array_bytes(rfc8746::typed_array<std::uint32_t, Order>{expected});
    const auto input = std::span<const std::byte>{bytes};
    for (std::size_t cut = 0; cut <= bytes.size(); ++cut) {
        CAPTURE(cut);
        input_chunks                       chunks;
        auto                               source  = make_input_source(chunks);
        auto                               decoder = make_resumable_decoder<codec::typed_array>(source);
        view_for<Order, decltype(decoder)> view;
        {
            auto operation = decoder(make_value(view));
            chunks.emplace_back(input.first(cut));
            operation.resume();
            if (!operation.done()) {
                CHECK(operation.committed_bytes() == 0);
                chunks.emplace_back(input.subspan(cut));
                operation.close_input();
            }

            REQUIRE(operation.done());
            REQUIRE(operation.result());
            CHECK(*operation.result() == bytes.size());
            CHECK(std::ranges::empty(operation.remaining_input()));
        }
        CHECK(std::ranges::equal(view.values(), expected));

        // The view keeps working when bound storage reallocates, during the operation or after it
        std::vector<std::byte>           storage(input.begin(), input.begin() + static_cast<std::ptrdiff_t>(cut));
        auto                             bound = make_resumable_decoder<codec::typed_array>(storage);
        view_for<Order, decltype(bound)> bound_view;
        {
            auto operation = bound(make_value(bound_view));
            if (!operation.done()) {
                CHECK(operation.committed_bytes() == 0);
            }
            storage.insert(storage.end(), input.begin() + static_cast<std::ptrdiff_t>(cut), input.end());
            REQUIRE(operation.close_input() == resume_state::finished);
            REQUIRE(operation.result());
            CHECK(*operation.result() == bytes.size());
        }
        storage.reserve(storage.capacity() * 2);
        CHECK(std::ranges::equal(bound_view.values(), expected));
    }
}

// An explicit codec hook remains the selected customization before this codec.
template <typename Self> struct typed_array_override : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;

    resumable_task<status_code> resumable_decode(rfc8746::typed_array<std::uint32_t> &array) {
        std::uint32_t value{};
        const auto    status = co_await static_cast<Self &>(*this)(value);
        if (status == status_code::success) {
            array.values() = {value};
        }
        co_return status;
    }

    resumable_task<status_code> resumable_encode(const rfc8746::typed_array<std::uint32_t> &array) {
        co_return co_await static_cast<Self &>(*this)(array.values().front());
    }
};

} // namespace

TEST_SUITE("roundtrip/resumable_typed_array") {
    TEST_CASE("typed arrays preserve non default constructible elements at every split in both byte orders") {
        auto check = []<rfc8746::typed_array_byte_order Order>() {
            const rfc8746::typed_array<std::uint32_t, Order> expected{0, 0x12345678, 0xffffffff};
            const auto                                       bytes = typed_array_bytes(expected);

            for (std::size_t cut = 0; cut <= bytes.size(); ++cut) {
                CAPTURE(cut);
                std::vector<std::byte>                              input(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut));
                auto                                                decoder = make_resumable_decoder<codec::typed_array>(input);
                rfc8746::typed_array<non_default_typed_word, Order> actual;
                auto                                                operation = decoder(actual);

                input.insert(input.end(), bytes.begin() + static_cast<std::ptrdiff_t>(cut), bytes.end());
                const auto state = operation.close_input();
                REQUIRE(state == resume_state::finished);
                REQUIRE(operation.result());
                CHECK(*operation.result() == bytes.size());
                REQUIRE(actual.values().size() == expected.values().size());
                for (std::size_t index = 0; index != expected.values().size(); ++index)
                    CHECK(actual.values()[index].value == expected.values()[index]);
            }
        };
        check.template operator()<rfc8746::typed_array_byte_order::little>();
        check.template operator()<rfc8746::typed_array_byte_order::big>();
    }

    TEST_CASE("borrowed destination ranges decode at every split in both byte orders") {
        const std::vector<std::uint32_t> expected{1, 0x12345678, 0xffffffff};
        auto                             check = [&]<rfc8746::typed_array_byte_order Order>() {
            auto wrap = [](auto &range) { return as_destination<Order>(range); };

            // Growable ranges drop what they held before
            check_destination_splits<Order>(expected, std::vector<std::uint32_t>{99, 98}, wrap);
            check_destination_splits<Order>(expected, std::deque<std::uint32_t>{99}, wrap);
            check_destination_splits<Order>(expected, std::list<std::uint32_t>{99}, wrap);
            check_destination_splits<Order>(expected, std::forward_list<std::uint32_t>{99}, wrap);

            // Fixed ranges of the right size are overwritten in place
            check_destination_splits<Order>(expected, std::array<std::uint32_t, 3>{99, 98, 97}, wrap);
            check_destination_splits<Order>(expected, std::array<std::uint32_t, 3>{99, 98, 97},
                                            [](auto &array) { return as_destination<Order>(std::span{array}); });
            check_destination_splits<Order>(expected, std::list<std::uint32_t>(3, 99),
                                            [](auto &list) { return as_destination<Order>(std::ranges::subrange(list)); });
        };
        check.template operator()<rfc8746::typed_array_byte_order::little>();
        check.template operator()<rfc8746::typed_array_byte_order::big>();
    }

    TEST_CASE("bounded typed arrays decode at every split") {
        using order = rfc8746::typed_array_byte_order;
        const std::vector<std::uint32_t> expected{1, 0x12345678, 0xffffffff};

        // Exact bounds keep both edges in play
        check_destination_splits<order::little>(expected, rfc8746::typed_array<std::uint32_t>{99},
                                                [](auto &array) { return as_bounded_size<3, 3>(array); });
        check_destination_splits<order::big>(expected, rfc8746::typed_array<std::uint32_t, order::big>{99},
                                             [](auto &array) { return as_bounded_size(array, 3, 3); });
        check_destination_splits<order::little>(expected, std::vector<std::uint32_t>{99},
                                                [](auto &values) { return as_bounded_size<3, 3>(rfc8746::as_typed_array(values)); });
        check_destination_splits<order::big>(expected, std::list<std::uint32_t>{99},
                                             [](auto &values) { return as_bounded_size(rfc8746::as_typed_array_be(values), 0, 3); });
    }

    TEST_CASE("bounded typed arrays encode at every split") {
        using order = rfc8746::typed_array_byte_order;
        const rfc8746::typed_array<std::uint32_t, order::big> expected{1, 0x12345678, 0xffffffff};
        const std::vector<std::uint32_t>                      values{1, 0x12345678, 0xffffffff};
        const std::list<std::uint32_t>                        linked(values.begin(), values.end());

        check_typed_array_encode_splits(expected, as_bounded_size<3, 3>(expected));
        check_typed_array_encode_splits(expected, as_bounded_size(expected, 3, 3));
        check_typed_array_encode_splits(expected, as_bounded_size<0, 3>(rfc8746::as_typed_array_be(values)));
        check_typed_array_encode_splits(expected, as_bounded_size(rfc8746::as_typed_array_be(linked), 3, 8));
    }

    TEST_CASE("typed array views decode at every split in both byte orders") {
        using order = rfc8746::typed_array_byte_order;
        const std::vector<std::uint32_t> expected{1, 0x12345678, 0xffffffff};

        // Passes the view itself, not a copy
        const auto plain = [](auto &view) -> auto & { return view; };
        check_view_splits<order::little>(expected, plain);
        check_view_splits<order::big>(expected, plain);
        check_view_splits<order::little>({}, plain);
    }

    TEST_CASE("bounded typed array views decode at every split") {
        using order = rfc8746::typed_array_byte_order;
        const std::vector<std::uint32_t> expected{1, 0x12345678, 0xffffffff};

        check_view_splits<order::little>(expected, [](auto &view) { return as_bounded_size<3, 3>(view); });
        check_view_splits<order::big>(expected, [](auto &view) { return as_bounded_size(view, 0, 3); });
    }

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
            auto                   sink      = make_output_buffer(chunks);
            auto                   operation = make_operation(sink);
            REQUIRE_FALSE(operation.done());
            for (auto &byte : output) {
                chunks.emplace_back(std::span{&byte, 1});
                operation.resume();
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
            input_storage.emplace_back(input_chunk{iterator, std::next(iterator)});
            decoder.resume();
        }
        REQUIRE(decoder.done());
        REQUIRE(decoder.result());
        CHECK(*decoder.result() == bytes.size());
        check_typed_array_values(actual, expected);

        std::list<std::byte> output(bytes.size());
        using output_chunk = std::ranges::subrange<std::list<std::byte>::iterator>;
        static_assert(!std::ranges::sized_range<output_chunk>);
        std::list<output_chunk> output_storage;
        auto                    sink    = make_output_buffer(output_storage);
        auto                    encoder = make_resumable_encoder<codec::typed_array>(sink)(expected);
        for (auto iterator = output.begin(); iterator != output.end(); ++iterator) {
            output_storage.emplace_back(output_chunk{iterator, std::next(iterator)});
            encoder.resume();
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
        chunks.emplace_back(counted_input_chunk{counted_typed_array_iterator{header.cbegin(), &increments},
                                                counted_typed_array_iterator{header.cend(), &increments}});
        operation.resume();
        REQUIRE_FALSE(operation.done());
        CHECK(actual.values().empty());
        CHECK(actual.values().capacity() >= expected.values().size());
        CHECK(increments == header.size());

        // A complete admitted payload catches both prewalks and repeated decoding.
        chunks.emplace_back(counted_input_chunk{counted_typed_array_iterator{payload.cbegin(), &increments},
                                                counted_typed_array_iterator{payload.cend(), &increments}});

        operation.resume();
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
            chunks.emplace_back(std::span{&byte, 1});
            decoder.resume();
        }
        REQUIRE(decoder.result());
        CHECK(*decoder.result() == reference.size());
        check_typed_array_values(actual, expected);

        std::vector<std::byte> output(reference.size());
        output_chunks          output_storage;
        auto                   sink    = make_output_buffer(output_storage);
        auto                   encoder = make_resumable_encoder<codec::typed_array>(sink)(expected);
        for (auto &byte : output) {
            output_storage.emplace_back(std::span{&byte, 1});
            encoder.resume();
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
        auto                   sink    = make_output_buffer(output_storage);
        auto                   encoder = make_resumable_encoder<typed_array_override, codec::typed_array>(sink)(expected);
        output_storage.emplace_back(std::span{output});
        encoder.close_output();
        REQUIRE(encoder.result());
        CHECK(*encoder.result() == reference.size());

        input_chunks                        input_storage;
        auto                                source = make_input_source(input_storage);
        rfc8746::typed_array<std::uint32_t> actual;
        auto                                decoder = make_resumable_decoder<typed_array_override, codec::typed_array>(source)(actual);
        input_storage.emplace_back(std::span<const std::byte>{output});
        decoder.close_input();
        REQUIRE(decoder.result());
        CHECK(*decoder.result() == reference.size());
        check_typed_array_values(actual, expected);
    }
}
