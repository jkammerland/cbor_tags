#include "test_util.h"

#include <array>
#include <bit>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/codec/typed_array.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <forward_list>
#include <iterator>
#include <list>
#include <memory>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

#if __has_include(<boost/circular_buffer.hpp>)
#include <boost/circular_buffer.hpp>
#define CBOR_TAGS_TEST_TYPED_ARRAY_CIRCULAR_BUFFER 1
#endif

using namespace cbor::tags;
using namespace cbor::tags::rfc8746;
using cbor::tags::test::detail::allocation_failure_guard;

namespace {

template <bool Sized> struct counted_bytes {
    using value_type = std::byte;

    struct iterator {
        using value_type        = std::byte;
        using difference_type   = std::ptrdiff_t;
        using iterator_concept  = std::bidirectional_iterator_tag;
        using iterator_category = std::bidirectional_iterator_tag;

        std::list<std::byte>::const_iterator current{};
        const counted_bytes                 *owner{};

        [[nodiscard]] const std::byte &operator*() const noexcept { return *current; }
        [[nodiscard]] const std::byte *operator->() const noexcept { return std::addressof(*current); }

        iterator &operator++() noexcept {
            ++current;
            ++owner->increments;
            return *this;
        }
        iterator operator++(int) noexcept {
            auto copy = *this;
            ++*this;
            return copy;
        }
        iterator &operator--() noexcept {
            --current;
            ++owner->decrements;
            return *this;
        }
        iterator operator--(int) noexcept {
            auto copy = *this;
            --*this;
            return copy;
        }

        friend bool operator==(const iterator &, const iterator &) = default;
    };

    std::list<std::byte> bytes;
    mutable std::size_t  increments{};
    mutable std::size_t  decrements{};

    explicit counted_bytes(const char *hex) {
        const auto parsed = to_bytes(hex);
        bytes.assign(parsed.begin(), parsed.end());
    }

    [[nodiscard]] iterator    begin() const noexcept { return {bytes.cbegin(), this}; }
    [[nodiscard]] iterator    end() const noexcept { return {bytes.cend(), this}; }
    [[nodiscard]] std::size_t size() const noexcept
        requires Sized
    {
        return bytes.size();
    }
};

static_assert(CborInputBuffer<counted_bytes<false>>);
static_assert(CborInputBuffer<counted_bytes<true>>);
static_assert(!std::ranges::sized_range<counted_bytes<false>>);
static_assert(std::ranges::sized_range<counted_bytes<true>>);
static_assert(!std::ranges::contiguous_range<counted_bytes<true>>);

template <typename T> struct limited_allocator {
    using value_type = T;

    limited_allocator() = default;
    template <typename U> constexpr limited_allocator(const limited_allocator<U> &) noexcept {}

    [[nodiscard]] T                    *allocate(std::size_t count) { return std::allocator<T>{}.allocate(count); }
    void                                deallocate(T *ptr, std::size_t count) noexcept { std::allocator<T>{}.deallocate(ptr, count); }
    [[nodiscard]] constexpr std::size_t max_size() const noexcept { return 3; }

    template <typename U> constexpr bool operator==(const limited_allocator<U> &) const noexcept { return true; }
};

template <bool Sized> void check_single_pass() {
    counted_bytes<Sized>      input{"d84a4c00000001fffffffe0000000307"};
    std::vector<std::int32_t> destination{99};
    destination.reserve(8);
    auto dec    = make_decoder<codec::typed_array>(input);
    auto result = expected<void, status_code>{};
    {
        allocation_failure_guard guard;
        result = dec(as_bounded_size(as_typed_array_be(destination), 1, 4));
    }

    REQUIRE(result);
    CHECK_EQ(destination, (std::vector<std::int32_t>{1, -2, 3}));
    CHECK_EQ(input.increments, 15U);
    CHECK_EQ(input.decrements, 0U);
    CHECK(dec.tell().current == std::prev(input.bytes.cend()));

    int following{};
    REQUIRE(dec(following));
    CHECK_EQ(following, 7);
    CHECK_EQ(input.increments, input.bytes.size());
}

template <bool Sized> void check_truncated_vector() {
    counted_bytes<Sized>      input{"d84e4c0100000002000000ff"};
    std::vector<std::int32_t> destination{91, 92, 93};
    const auto                result = make_decoder<codec::typed_array>(input)(as_typed_array(destination));

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::incomplete);
    CHECK_EQ(input.decrements, 0U);
    if constexpr (Sized) {
        CHECK_EQ(destination, (std::vector<std::int32_t>{91, 92, 93}));
        CHECK_EQ(input.increments, 3U);
    } else {
        CHECK_EQ(destination, (std::vector<std::int32_t>{1, 2}));
        CHECK_EQ(input.increments, input.bytes.size());
    }
}

template <bool Sized> void check_truncated_span() {
    counted_bytes<Sized>        input{"d84e4c0100000002000000ff"};
    std::array<std::int32_t, 3> destination{91, 92, 93};
    auto                        dec    = make_decoder<codec::typed_array>(input);
    const auto                  result = dec(as_typed_array(std::span{destination}));

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::incomplete);
    CHECK_EQ(input.decrements, 0U);
    if constexpr (Sized) {
        CHECK_EQ(destination, (std::array<std::int32_t, 3>{91, 92, 93}));
        CHECK_EQ(input.increments, 3U);
        CHECK(dec.tell().current == std::next(input.bytes.cbegin(), 3));
    } else {
        CHECK_EQ(destination, (std::array<std::int32_t, 3>{1, 2, 93}));
        CHECK_EQ(input.increments, input.bytes.size());
        CHECK(dec.tell() == input.end());
    }
}

#ifdef CBOR_TAGS_TEST_TYPED_ARRAY_CIRCULAR_BUFFER
template <typename Container> void check_circular_buffer_destination() {
    // An exact-capacity payload replaces even an already-full destination.
    Container exact(2);
    exact.push_back(91);
    exact.push_back(92);
    const auto exact_input = to_bytes("d84e4801000000feffffff07");
    auto       exact_dec   = make_decoder<codec::typed_array>(exact_input);
    REQUIRE(exact_dec(as_typed_array(exact)));
    CHECK(std::ranges::equal(exact, std::array<std::int32_t, 2>{1, -2}));
    int following{};
    REQUIRE(exact_dec(following));
    CHECK_EQ(following, 7);

    // Reject overflow before clearing existing elements or reading the payload.
    Container overflow(2);
    overflow.push_back(91);
    const auto overflow_input  = to_bytes("d84e4c010000000200000003000000");
    auto       overflow_dec    = make_decoder<codec::typed_array>(overflow_input);
    const auto overflow_result = overflow_dec(as_typed_array(overflow));
    REQUIRE_FALSE(overflow_result);
    CHECK_EQ(overflow_result.error(), status_code::size_limit_exceeded);
    CHECK(std::ranges::equal(overflow, std::array<std::int32_t, 1>{91}));
    CHECK(overflow_dec.tell() == overflow_input.cbegin() + 3);

    for (const auto &[hex, succeeds] : {std::pair{"d84e4007", true}, std::pair{"d84e4401000000", false}}) {
        CAPTURE(hex);
        Container empty;
        REQUIRE_EQ(static_cast<std::size_t>(empty.capacity()), 0U);
        const auto input  = to_bytes(hex);
        auto       dec    = make_decoder<codec::typed_array>(input);
        const auto result = dec(as_typed_array(empty));
        if (succeeds) {
            REQUIRE(result);
            REQUIRE(dec(following));
            CHECK_EQ(following, 7);
        } else {
            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), status_code::size_limit_exceeded);
            CHECK(dec.tell() == input.cbegin() + 3);
        }
        CHECK(empty.empty());
    }
}
#endif

} // namespace

TEST_SUITE("cbor_wire/typed_array_destinations") {

    TEST_CASE("typed array destinations decode independently specified endian payloads") {
        SUBCASE("little endian int32 and trailing item") {
            const auto                input = to_bytes("d84e4c01000000feffffff0300000007");
            std::vector<std::int32_t> destination{99, 98, 97, 96};
            auto                      dec = make_decoder<codec::typed_array>(input);
            REQUIRE(dec(as_typed_array(destination)));
            CHECK_EQ(destination, (std::vector<std::int32_t>{1, -2, 3}));
            CHECK(dec.tell() == input.cbegin() + 15);

            int following{};
            REQUIRE(dec(following));
            CHECK_EQ(following, 7);
        }

        SUBCASE("big endian int32 into an exact span") {
            const auto                  input = to_bytes("d84a4c00000001fffffffe00000003");
            std::array<std::int32_t, 3> destination{};
            REQUIRE(make_decoder<codec::typed_array>(input)(as_typed_array_be(std::span{destination})));
            CHECK_EQ(destination, (std::array<std::int32_t, 3>{1, -2, 3}));
        }

        SUBCASE("float bit representations") {
            const auto         input = to_bytes("d8554c000000804523c17f000020c0");
            std::vector<float> destination;
            REQUIRE(make_decoder<codec::typed_array>(input)(as_typed_array(destination)));
            REQUIRE_EQ(destination.size(), 3U);
            CHECK_EQ(std::bit_cast<std::uint32_t>(destination[0]), 0x80000000U);
            CHECK_EQ(std::bit_cast<std::uint32_t>(destination[1]), 0x7FC12345U);
            CHECK_EQ(std::bit_cast<std::uint32_t>(destination[2]), 0xC0200000U);
        }

        SUBCASE("empty vector and span") {
            const auto                input = to_bytes("d84e4007");
            std::vector<std::int32_t> destination{99};
            auto                      dec = make_decoder<codec::typed_array>(input);
            REQUIRE(dec(as_typed_array(destination)));
            CHECK(destination.empty());
            CHECK(dec.tell() == input.cbegin() + 3);

            std::span<std::int32_t, 0> empty;
            REQUIRE(make_decoder<codec::typed_array>(input)(as_typed_array(empty)));
        }
    }

    TEST_CASE("typed array destinations reject malformed headers without changing storage") {
        for (const auto &[hex, expected_status] : {
                 std::pair{"80", status_code::no_match_for_tag_on_buffer},
                 std::pair{"d84f4400000000", status_code::no_match_for_tag},
                 std::pair{"d84e80", status_code::no_match_for_bstr_on_buffer},
                 std::pair{"d84e5f", status_code::no_match_for_bstr_on_buffer},
                 std::pair{"d84e450100000000", status_code::unexpected_group_size},
                 std::pair{"dc", status_code::invalid_additional_info},
                 std::pair{"d84e5c", status_code::invalid_additional_info},
                 std::pair{"d8", status_code::incomplete},
                 std::pair{"d84e58", status_code::incomplete},
             }) {
            CAPTURE(hex);
            const auto                  input = to_bytes(hex);
            std::vector<std::int32_t>   vector_output{91, 92, 93};
            std::array<std::int32_t, 3> span_output{91, 92, 93};
            const auto                  vector_result = make_decoder<codec::typed_array>(input)(as_typed_array(vector_output));
            const auto                  span_result   = make_decoder<codec::typed_array>(input)(as_typed_array(std::span{span_output}));

            REQUIRE_FALSE(vector_result);
            REQUIRE_FALSE(span_result);
            CHECK_EQ(vector_result.error(), expected_status);
            CHECK_EQ(span_result.error(), expected_status);
            CHECK_EQ(vector_output, (std::vector<std::int32_t>{91, 92, 93}));
            CHECK_EQ(span_output, (std::array<std::int32_t, 3>{91, 92, 93}));
        }
    }

    TEST_CASE("typed array span count mismatches reject before the payload") {
        for (const char *hex : {"d84e480100000002000000", "d84e5001000000020000000300000004000000"}) {
            CAPTURE(hex);
            const auto                  input = to_bytes(hex);
            std::array<std::int32_t, 3> destination{91, 92, 93};
            auto                        fixed_dec    = make_decoder<codec::typed_array>(input);
            auto                        dynamic_dec  = make_decoder<codec::typed_array>(input);
            const auto                  fixed_result = fixed_dec(as_typed_array(std::span{destination}));
            const auto dynamic_result = dynamic_dec(as_bounded_size(as_typed_array(std::span<std::int32_t>{destination}), 0, 8));

            REQUIRE_FALSE(fixed_result);
            REQUIRE_FALSE(dynamic_result);
            CHECK_EQ(fixed_result.error(), status_code::unexpected_group_size);
            CHECK_EQ(dynamic_result.error(), status_code::unexpected_group_size);
            CHECK_EQ(destination, (std::array<std::int32_t, 3>{91, 92, 93}));
            CHECK(fixed_dec.tell() == input.cbegin() + 3);
            CHECK(dynamic_dec.tell() == input.cbegin() + 3);
        }
    }

    TEST_CASE("typed array element bounds reject before destination mutation") {
        const auto                input = to_bytes("d84e4c010000000200000003000000");
        std::vector<std::int32_t> destination{91};
        auto                      static_dec     = make_decoder<codec::typed_array>(input);
        auto                      dynamic_dec    = make_decoder<codec::typed_array>(input);
        const auto                static_result  = static_dec(as_bounded_size<0, 2>(as_typed_array(destination)));
        const auto                dynamic_result = dynamic_dec(as_bounded_size(as_typed_array(destination), 4, 8));

        REQUIRE_FALSE(static_result);
        REQUIRE_FALSE(dynamic_result);
        CHECK_EQ(static_result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(dynamic_result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(destination, (std::vector<std::int32_t>{91}));
        CHECK(static_dec.tell() == input.cbegin() + 3);
        CHECK(dynamic_dec.tell() == input.cbegin() + 3);
    }

    TEST_CASE("typed array destinations honor allocator size limits before payload admission") {
        const auto                                                 input = to_bytes("d84e5001000000");
        std::vector<std::int32_t, limited_allocator<std::int32_t>> destination{91, 92};
        auto                                                       dec    = make_decoder<codec::typed_array>(input);
        const auto                                                 result = dec(as_typed_array(destination));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK(std::ranges::equal(destination, std::array<std::int32_t, 2>{91, 92}));
        CHECK(dec.tell() == input.cbegin() + 3);
    }

#ifdef CBOR_TAGS_TEST_TYPED_ARRAY_CIRCULAR_BUFFER
    TEST_CASE("typed array circular buffer destinations reject overwrite at capacity") {
        check_circular_buffer_destination<boost::circular_buffer<std::int32_t>>();
    }

    TEST_CASE("typed array space optimized circular buffer destinations reject overwrite at capacity") {
        check_circular_buffer_destination<boost::circular_buffer_space_optimized<std::int32_t>>();
    }
#endif

    TEST_CASE("typed array contiguous truncation retains destination and payload position") {
        const auto                  input = to_bytes("d84e4c0100000002000000ff");
        std::vector<std::int32_t>   vector_output{91, 92, 93};
        std::array<std::int32_t, 3> span_output{91, 92, 93};
        auto                        vector_dec    = make_decoder<codec::typed_array>(input);
        auto                        span_dec      = make_decoder<codec::typed_array>(input);
        const auto                  vector_result = vector_dec(as_typed_array(vector_output));
        const auto                  span_result   = span_dec(as_typed_array(std::span{span_output}));

        REQUIRE_FALSE(vector_result);
        REQUIRE_FALSE(span_result);
        CHECK_EQ(vector_result.error(), status_code::incomplete);
        CHECK_EQ(span_result.error(), status_code::incomplete);
        CHECK_EQ(vector_output, (std::vector<std::int32_t>{91, 92, 93}));
        CHECK_EQ(span_output, (std::array<std::int32_t, 3>{91, 92, 93}));
        CHECK(vector_dec.tell() == input.cbegin() + 3);
        CHECK(span_dec.tell() == input.cbegin() + 3);
    }

    TEST_CASE("typed array destinations consume non-contiguous input once") {
        check_single_pass<true>();
        check_single_pass<false>();
        check_truncated_vector<true>();
        check_truncated_vector<false>();
        check_truncated_span<true>();
        check_truncated_span<false>();
    }

    TEST_CASE("typed array unsized destination growth follows completed elements") {
        // A huge declared length cannot trigger reservation before the few admitted bytes.
        counted_bytes<false>      input{"d84e5a4000000001000000ff"};
        std::vector<std::int32_t> destination{99};
        destination.reserve(8);
        const auto original_capacity = destination.capacity();
        auto       dec               = make_decoder<codec::typed_array>(input);
        auto       result            = expected<void, status_code>{};
        {
            allocation_failure_guard guard;
            result = dec(as_typed_array(destination));
        }

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
        CHECK_EQ(destination, (std::vector<std::int32_t>{1}));
        CHECK_EQ(destination.capacity(), original_capacity);
        CHECK_EQ(input.increments, input.bytes.size());
        CHECK_EQ(input.decrements, 0U);
        CHECK(dec.tell() == input.end());
    }

    TEST_CASE("typed array unsized non-contiguous destinations retain only complete elements") {
        auto check_destination = [](auto destination) {
            counted_bytes<false> input{"d84e4c0100000002000000ff"};
            auto                 dec    = make_decoder<codec::typed_array>(input);
            const auto           result = dec(as_typed_array(destination));

            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), status_code::incomplete);
            CHECK(std::ranges::equal(destination, std::array<std::int32_t, 2>{1, 2}));
            CHECK_EQ(input.increments, input.bytes.size());
            CHECK_EQ(input.decrements, 0U);
            CHECK(dec.tell() == input.end());
        };

        check_destination(std::deque<std::int32_t>{91, 92, 93});
        check_destination(std::list<std::int32_t>{91, 92, 93});
        check_destination(std::forward_list<std::int32_t>{91, 92, 93});
    }

    TEST_CASE("typed array destinations support dispatch after the tag is consumed") {
        const auto                input = to_bytes("d84e4c01000000feffffff03000000");
        std::vector<std::int32_t> output;
        auto                      wrapped = as_typed_array(output);
        auto                      bounded = as_bounded_size<1, 3>(as_typed_array(output));
        auto                      dynamic = as_bounded_size(as_typed_array(output), 1, 3);

        auto check_dispatch = [&](auto &target) {
            auto dec                 = make_decoder<codec::typed_array>(input);
            const auto [major, info] = dec.read_initial_byte();
            REQUIRE_EQ(major, major_type::Tag);
            const auto tag = dec.decode_unsigned(info);
            CHECK_EQ(dec.decode(target, tag), status_code::success);
            CHECK_EQ(output, (std::vector<std::int32_t>{1, -2, 3}));
            CHECK(dec.tell() == input.cend());
            output.clear();
        };

        check_dispatch(wrapped);
        check_dispatch(bounded);
        check_dispatch(dynamic);
    }

    TEST_CASE("ordinary vectors still require an ordinary cbor array") {
        const auto                input = to_bytes("d84e4c010000000200000003000000");
        std::vector<std::int32_t> destination;
        const auto                result = make_decoder<codec::typed_array>(input)(destination);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_array_on_buffer);
        CHECK(destination.empty());
    }

} // TEST_SUITE
