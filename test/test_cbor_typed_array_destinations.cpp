#include "test_util.h"

#include <array>
#include <bit>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/codec/typed_array.h>
#include <cbor_tags/extensions/cbor_visualization.h>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <fmt/format.h>
#include <forward_list>
#include <iterator>
#include <list>
#include <map>
#include <memory_resource>
#include <ranges>
#include <set>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#if __has_include(<boost/container/small_vector.hpp>) && __has_include(<boost/container/static_vector.hpp>)
#include <boost/container/small_vector.hpp>
#include <boost/container/static_vector.hpp>
#define CBOR_TAGS_TEST_TYPED_ARRAY_BOOST 1
#endif

namespace typed_array_destination_test {

struct deep_const_view : std::ranges::view_interface<deep_const_view> {
    std::list<std::int32_t> *values;

    explicit deep_const_view(std::list<std::int32_t> &sequence) noexcept : values(&sequence) {}

    auto begin() noexcept { return values->begin(); }
    auto end() noexcept { return values->end(); }
    auto begin() const noexcept { return values->cbegin(); }
    auto end() const noexcept { return values->cend(); }
};

struct asymmetric_capability_view : std::ranges::view_interface<asymmetric_capability_view> {
    using const_iterator = std::common_iterator<std::counted_iterator<std::int32_t *>, std::default_sentinel_t>;

    std::span<std::int32_t> elements;

    explicit asymmetric_capability_view(std::span<std::int32_t> values) noexcept : elements(values) {}

    auto data() noexcept { return elements.data(); }
    auto begin() noexcept { return elements.data(); }
    auto end() noexcept { return elements.data() + elements.size(); }
    auto begin() const noexcept {
        return const_iterator{std::counted_iterator{elements.data(), static_cast<std::ptrdiff_t>(elements.size())}};
    }
    auto end() const noexcept { return const_iterator{std::default_sentinel}; }
    auto size() const noexcept { return elements.size(); }
};

struct appendable_view : std::ranges::view_interface<appendable_view> {
    std::vector<std::int32_t> *values;
    std::size_t                limit;

    appendable_view(std::vector<std::int32_t> &sequence, std::size_t max) noexcept : values(&sequence), limit(max) {}

    auto begin() const noexcept { return values->begin(); }
    auto end() const noexcept { return values->end(); }
    auto size() const noexcept { return values->size(); }

    void clear() noexcept { values->clear(); }
    void push_back(std::int32_t value) { values->push_back(value); }
    auto max_size() noexcept { return limit; }
};

} // namespace typed_array_destination_test

template <> inline constexpr bool std::ranges::enable_borrowed_range<typed_array_destination_test::deep_const_view>            = true;
template <> inline constexpr bool std::ranges::enable_borrowed_range<typed_array_destination_test::asymmetric_capability_view> = true;
template <> inline constexpr bool std::ranges::enable_borrowed_range<typed_array_destination_test::appendable_view>            = true;

using namespace cbor::tags;
using namespace cbor::tags::rfc8746;
using cbor::tags::test::detail::allocation_failure_guard;

namespace {

template <typename T>
concept CanWrapDestination = requires(T &&value) { as_typed_array(std::forward<T>(value)); };

template <typename T>
concept CanWrapBigEndianDestination = requires(T &&value) { as_typed_array_be(std::forward<T>(value)); };

using vector_destination = decltype(as_typed_array(std::declval<std::vector<std::int32_t> &>()));
using span_destination   = decltype(as_typed_array(std::declval<std::span<std::int32_t, 3>>()));

// Borrowing descriptors stays constexpr even when the backing elements are mutable.
std::int32_t   constexpr_backing[2]{};
constexpr auto constexpr_span    = as_typed_array(std::span<std::int32_t>{constexpr_backing});
constexpr auto constexpr_span_be = as_typed_array_be(std::span<std::int32_t>{constexpr_backing});
static_assert(constexpr_span.values().size() == 2);
static_assert(constexpr_span_be.values().size() == 2);
static_assert(constexpr_span.values().data() == constexpr_backing);
static_assert(constexpr_span_be.values().data() == constexpr_backing);
static_assert(std::same_as<std::ranges::range_reference_t<decltype(constexpr_span.values())>, std::int32_t &>);

constexpr typed_array_ref<std::int32_t>                              constexpr_readonly    = constexpr_span;
constexpr typed_array_ref<std::int32_t, typed_array_byte_order::big> constexpr_readonly_be = constexpr_span_be;
static_assert(constexpr_readonly.values().size() == 2);
static_assert(constexpr_readonly_be.values().size() == 2);
static_assert(constexpr_readonly.values().data() == constexpr_backing);
static_assert(constexpr_readonly_be.values().data() == constexpr_backing);

constexpr auto constexpr_subrange    = as_typed_array(std::ranges::subrange{constexpr_backing, constexpr_backing + 2});
constexpr auto constexpr_subrange_be = as_typed_array_be(std::ranges::subrange{constexpr_backing, constexpr_backing + 2});
static_assert(constexpr_subrange.values().size() == 2);
static_assert(constexpr_subrange_be.values().size() == 2);

// Reference-backed wrappers retain constexpr access to their original binding.
std::vector<std::int32_t> constexpr_bound_vector;
constexpr auto            constexpr_vector    = as_typed_array(constexpr_bound_vector);
constexpr auto            constexpr_vector_be = as_typed_array_be(constexpr_bound_vector);
constexpr auto            constexpr_array     = as_typed_array(constexpr_backing);
static_assert(&constexpr_vector.values() == &constexpr_bound_vector);
static_assert(&constexpr_vector_be.values() == &constexpr_bound_vector);
static_assert(std::ranges::size(constexpr_array.values()) == 2);

static_assert(!std::default_initializable<vector_destination>);
static_assert(!std::default_initializable<span_destination>);
static_assert(CanWrapDestination<const std::vector<std::int32_t> &>);
static_assert(!CanWrapDestination<std::vector<std::int32_t>>);
static_assert(!CanWrapDestination<const std::vector<std::int32_t>>);
static_assert(CanWrapDestination<std::span<std::int32_t, 3>>);
static_assert(CanWrapDestination<const std::span<std::int32_t, 3> &>);
static_assert(CanWrapDestination<std::span<const std::int32_t>>);
static_assert(CanWrapBigEndianDestination<const std::vector<std::int32_t> &>);
static_assert(!CanWrapBigEndianDestination<std::vector<std::int32_t>>);
static_assert(!CanWrapBigEndianDestination<const std::vector<std::int32_t>>);
static_assert(CanWrapBigEndianDestination<std::span<std::int32_t, 3>>);
static_assert(CanWrapBigEndianDestination<const std::span<std::int32_t, 3> &>);
static_assert(CanWrapDestination<std::deque<std::int32_t> &>);
static_assert(CanWrapDestination<std::list<std::int32_t> &>);
static_assert(CanWrapDestination<std::array<std::int32_t, 3> &>);
static_assert(!CanWrapDestination<std::deque<std::int32_t>>);
static_assert(!CanWrapDestination<std::list<std::int32_t>>);
static_assert(!CanWrapDestination<std::vector<bool> &>);
static_assert(!CanWrapDestination<std::set<std::int32_t> &>);
static_assert(!CanWrapDestination<std::map<std::int32_t, std::int32_t> &>);

struct counting_resource : std::pmr::memory_resource {
    std::size_t allocations{};

  private:
    void *do_allocate(std::size_t bytes, std::size_t alignment) override {
        ++allocations;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void *ptr, std::size_t bytes, std::size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(ptr, bytes, alignment);
    }

    bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override { return this == &other; }
};

template <typename T, typed_array_byte_order Order, typename Storage> auto wrap(Storage &&storage) {
    if constexpr (Order == typed_array_byte_order::big) {
        return as_typed_array_be(std::forward<Storage>(storage));
    } else {
        return as_typed_array(std::forward<Storage>(storage));
    }
}

template <typed_array_byte_order Order = typed_array_byte_order::little, typename T>
std::vector<std::byte> encoded_values(const std::vector<T> &source) {
    std::vector<std::byte> bytes;
    REQUIRE(make_encoder<codec::typed_array>(bytes)(wrap<T, Order>(source)));
    return bytes;
}

template <typename T> void check_values(std::span<const T> observed, std::span<const T> expected) {
    REQUIRE_EQ(observed.size(), expected.size());
    for (std::size_t index = 0; index < observed.size(); ++index) {
        if constexpr (std::same_as<T, float16_t>) {
            CHECK_EQ(observed[index].value, expected[index].value);
        } else if constexpr (std::floating_point<T>) {
            // Compare representations so signed zero and NaN payloads are exercised.
            CHECK_EQ((std::bit_cast<std::array<std::byte, sizeof(T)>>(observed[index])),
                     (std::bit_cast<std::array<std::byte, sizeof(T)>>(expected[index])));
        } else {
            CHECK_EQ(observed[index], expected[index]);
        }
    }
}

template <typed_array_byte_order Order, typename T> void check_destinations(const std::vector<T> &source) {
    const auto bytes = encoded_values<Order>(source);

    std::vector<T> vector_output(8);
    vector_output.reserve(16);
    const auto *original_data     = vector_output.data();
    const auto  original_capacity = vector_output.capacity();
    REQUIRE(make_decoder<codec::typed_array>(bytes)(wrap<T, Order>(vector_output)));
    check_values<T>(vector_output, source);
    CHECK_EQ(vector_output.data(), original_data);
    CHECK_EQ(vector_output.capacity(), original_capacity);

    std::array<T, 3> fixed_output{};
    REQUIRE_EQ(source.size(), fixed_output.size());
    REQUIRE(make_decoder<codec::typed_array>(bytes)(wrap<T, Order>(std::span<T, 3>{fixed_output})));
    check_values<T>(fixed_output, source);

    std::array<T, 3> dynamic_output{};
    const auto       descriptor = std::span<T>{dynamic_output};
    REQUIRE(make_decoder<codec::typed_array>(bytes)(wrap<T, Order>(descriptor)));
    check_values<T>(dynamic_output, source);
}

template <typename T> std::string schema() {
    fmt::memory_buffer output;
    cddl::schema_to<T>(output, {.row_options = {.format_by_rows = false}});
    return fmt::to_string(output);
}

template <typename Container> void check_sequence_destination() {
    const std::vector<std::int32_t> source{1, -2, 3};
    const auto                      bytes = encoded_values(source);
    Container                       output{99, 98};
    REQUIRE(make_decoder<codec::typed_array>(bytes)(as_typed_array(output)));
    CHECK(std::ranges::equal(output, source));

    std::vector<std::byte> reencoded;
    REQUIRE(make_encoder<codec::typed_array>(reencoded)(as_typed_array(std::as_const(output))));
    std::vector<std::int32_t> copied;
    REQUIRE(make_decoder<codec::typed_array>(reencoded)(as_typed_array(copied)));
    CHECK_EQ(copied, source);

    reencoded.clear();
    copied.clear();
    REQUIRE(make_encoder<codec::typed_array>(reencoded)(as_bounded_size<1, 3>(as_typed_array(output))));
    REQUIRE(make_decoder<codec::typed_array>(reencoded)(as_typed_array(copied)));
    CHECK_EQ(copied, source);

    const std::vector<std::int32_t> empty;
    const auto                      empty_bytes = encoded_values(empty);
    REQUIRE(make_decoder<codec::typed_array>(empty_bytes)(as_typed_array(output)));
    CHECK(output.empty());
}

template <typed_array_byte_order Order> void check_asymmetric_capability_view() {
    using view_type = typed_array_destination_test::asymmetric_capability_view;
    static_assert(std::ranges::contiguous_range<view_type>);
    static_assert(std::ranges::forward_range<const view_type>);
    static_assert(!std::ranges::contiguous_range<const view_type>);
    static_assert(std::same_as<std::ranges::range_reference_t<view_type>, std::ranges::range_reference_t<const view_type>>);

    const std::vector<std::int32_t> source{1, -2, 3};
    const auto                      bytes = encoded_values<Order>(source);
    std::array<std::int32_t, 3>     backing{};
    auto                            wrapped = wrap<std::int32_t, Order>(view_type{backing});
    REQUIRE(make_decoder<codec::typed_array>(bytes)(wrapped));
    check_values<std::int32_t>(backing, source);

    const auto                          &const_wrapper = wrapped;
    typed_array_ref<std::int32_t, Order> readonly      = const_wrapper;
    CHECK_EQ(readonly.values().data(), backing.data());
    CHECK_EQ(readonly.values().size(), backing.size());

    std::vector<std::byte> reencoded;
    REQUIRE(make_encoder<codec::typed_array>(reencoded)(const_wrapper));
    std::vector<std::int32_t> copied;
    REQUIRE(make_decoder<codec::typed_array>(reencoded)(wrap<std::int32_t, Order>(copied)));
    CHECK_EQ(copied, source);
}

} // namespace

TEST_SUITE("roundtrip/typed_array_destinations") {

    TEST_CASE("mutable typed array destinations roundtrip representative element representations") {
        check_destinations<typed_array_byte_order::little>(std::vector<std::int16_t>{-3, 0, 257});
        check_destinations<typed_array_byte_order::big>(std::vector<std::int16_t>{-3, 0, 257});
        check_destinations<typed_array_byte_order::little>(std::vector<std::uint64_t>{0, 1, 0xFFEEDDCCBBAA9988ULL});
        check_destinations<typed_array_byte_order::big>(std::vector<std::uint64_t>{0, 1, 0xFFEEDDCCBBAA9988ULL});
        check_destinations<typed_array_byte_order::little>(std::vector<std::uint8_t>{0, 128, 255});
        check_destinations<typed_array_byte_order::little>(
            std::vector<uint8_clamped>{uint8_clamped{0}, uint8_clamped{128}, uint8_clamped{255}});

        const std::vector<float> floats{std::bit_cast<float>(std::uint32_t{0x80000000}), std::bit_cast<float>(std::uint32_t{0x7FC12345}),
                                        -2.5F};
        check_destinations<typed_array_byte_order::little>(floats);
        check_destinations<typed_array_byte_order::big>(floats);
        check_destinations<typed_array_byte_order::little>(std::vector<double>{-0.0, 1.5, -2.25});
        check_destinations<typed_array_byte_order::big>(std::vector<double>{-0.0, 1.5, -2.25});

        const std::vector<float16_t> halves{float16_t{std::uint16_t{0x8000}}, float16_t{std::uint16_t{0x7E01}},
                                            float16_t{std::uint16_t{0x3C00}}};
        check_destinations<typed_array_byte_order::little>(halves);
        check_destinations<typed_array_byte_order::big>(halves);

        float128_t quad{};
        for (std::size_t index = 0; index < quad.bytes.size(); ++index) {
            quad.bytes[index] = static_cast<std::byte>(index);
        }
        check_destinations<typed_array_byte_order::little>(std::vector<float128_t>{quad, {}, quad});
        check_destinations<typed_array_byte_order::big>(std::vector<float128_t>{quad, {}, quad});
    }

    TEST_CASE("mutable typed array vectors retain their allocator and allocate only when growth is needed") {
        const std::vector<std::int32_t> source{1, -2, 3};
        const auto                      bytes = encoded_values(source);
        counting_resource               resource;
        std::pmr::vector<std::int32_t>  destination{&resource};

        REQUIRE(make_decoder<codec::typed_array>(bytes)(as_typed_array(destination)));
        check_values<std::int32_t>(destination, source);
        CHECK_EQ(destination.get_allocator().resource(), &resource);
        CHECK_GT(resource.allocations, 0U);

        destination.reserve(32);
        destination.assign(8, 99);
        const auto  allocations   = resource.allocations;
        const auto *original_data = destination.data();
        auto        dec           = make_decoder<codec::typed_array>(bytes);
        auto        result        = expected<void, status_code>{};
        {
            allocation_failure_guard guard;
            result = dec(as_typed_array(destination));
        }

        REQUIRE(result);
        check_values<std::int32_t>(destination, source);
        CHECK_EQ(destination.data(), original_data);
        CHECK_EQ(destination.get_allocator().resource(), &resource);
        CHECK_EQ(resource.allocations, allocations);
    }

    TEST_CASE("mutable typed array spans and bounded vectors decode without temporary allocations") {
        const std::vector<std::int32_t> source{1, -2, 3};
        const auto                      bytes = encoded_values<typed_array_byte_order::big>(source);
        std::array<std::int32_t, 3>     fixed_output{};
        std::vector<std::int32_t>       vector_output;
        vector_output.reserve(8);

        auto span_dec      = make_decoder<codec::typed_array>(bytes);
        auto vector_dec    = make_decoder<codec::typed_array>(bytes);
        auto span_result   = expected<void, status_code>{};
        auto vector_result = expected<void, status_code>{};
        {
            allocation_failure_guard guard;
            span_result   = span_dec(as_typed_array_be(std::span{fixed_output}));
            vector_result = vector_dec(as_bounded_size<1, 4>(as_typed_array_be(vector_output)));
        }

        REQUIRE(span_result);
        REQUIRE(vector_result);
        check_values<std::int32_t>(fixed_output, source);
        check_values<std::int32_t>(vector_output, source);
    }

    TEST_CASE("mutable typed array destinations compose with static and runtime bounds") {
        const std::vector<std::int32_t> source{1, -2, 3};
        const auto                      bytes = encoded_values(source);
        std::vector<std::int32_t>       static_output;
        std::vector<std::int32_t>       dynamic_output;
        std::array<std::int32_t, 3>     span_output{};

        REQUIRE(make_decoder<codec::typed_array>(bytes)(as_bounded_size<1, 3>(as_typed_array(static_output))));
        REQUIRE(make_decoder<codec::typed_array>(bytes)(as_bounded_size(as_typed_array(dynamic_output), 1, 3)));
        REQUIRE(make_decoder<codec::typed_array>(bytes)(as_bounded_size(as_typed_array(std::span{span_output}), 3, 3)));
        check_values<std::int32_t>(static_output, source);
        check_values<std::int32_t>(dynamic_output, source);
        check_values<std::int32_t>(span_output, source);
    }

    TEST_CASE("mutable typed array spans copy their descriptor and accept const descriptors") {
        const std::vector<std::int32_t> source{1, -2, 3};
        const auto                      bytes = encoded_values(source);
        std::array<std::int32_t, 3>     original{9, 9, 9};
        std::array<std::int32_t, 3>     other{8, 8, 8};
        auto                            descriptor = std::span{original};
        auto                            wrapped    = as_typed_array(descriptor);
        descriptor                                 = std::span{other};

        REQUIRE(make_decoder<codec::typed_array>(bytes)(wrapped));
        check_values<std::int32_t>(original, source);
        CHECK_EQ(other, (std::array<std::int32_t, 3>{8, 8, 8}));

        const auto const_descriptor = std::span{other};
        REQUIRE(make_decoder<codec::typed_array>(bytes)(as_typed_array(const_descriptor)));
        check_values<std::int32_t>(other, source);
    }

    TEST_CASE("mutable typed array borrowed views retain their own descriptor") {
        const std::vector<std::int32_t> source{1, -2, 3};
        const auto                      bytes = encoded_values(source);
        std::array<std::int32_t, 3>     original{9, 9, 9};
        std::array<std::int32_t, 3>     other{8, 8, 8};
        auto                            descriptor = std::ranges::subrange(original.begin(), original.end());
        auto                            wrapped    = as_typed_array(descriptor);
        descriptor                                 = std::ranges::subrange(other.begin(), other.end());

        REQUIRE(make_decoder<codec::typed_array>(bytes)(wrapped));
        check_values<std::int32_t>(original, source);
        CHECK_EQ(other, (std::array<std::int32_t, 3>{8, 8, 8}));

        auto wrapped_after_descriptor_lifetime = [&] {
            auto local_descriptor = std::ranges::subrange(other.begin(), other.end());
            return as_typed_array(local_descriptor);
        }();
        REQUIRE(make_decoder<codec::typed_array>(bytes)(wrapped_after_descriptor_lifetime));
        check_values<std::int32_t>(other, source);
    }

    TEST_CASE("mutable typed array borrowed views support nonconst iteration") {
        const std::vector<std::int32_t> source{1, -2, 3};
        const auto                      bytes = encoded_values(source);
        std::list<std::int32_t>         backing{77, 9, 9, 9};
        using drop_type = decltype(std::views::drop(backing, 1));
        static_assert(std::ranges::borrowed_range<drop_type>);
        static_assert(std::ranges::sized_range<drop_type>);
        static_assert(!std::ranges::range<const drop_type>);

        REQUIRE(make_decoder<codec::typed_array>(bytes)(as_typed_array(std::views::drop(backing, 1))));
        CHECK(std::ranges::equal(backing, std::array<std::int32_t, 4>{77, 1, -2, 3}));

        std::vector<std::byte> reencoded;
        REQUIRE(make_encoder<codec::typed_array>(reencoded)(as_typed_array(std::views::drop(backing, 1))));
        std::vector<std::int32_t> copied;
        REQUIRE(make_decoder<codec::typed_array>(reencoded)(as_typed_array(copied)));
        CHECK_EQ(copied, source);
    }

    TEST_CASE("typed array borrowed views preserve deep const source semantics") {
        const std::vector<std::int32_t>               source{1, -2, 3};
        const auto                                    bytes = encoded_values(source);
        std::list<std::int32_t>                       backing{91, 92, 93};
        typed_array_destination_test::deep_const_view writable{backing};
        REQUIRE(make_decoder<codec::typed_array>(bytes)(as_typed_array(writable)));
        CHECK(std::ranges::equal(backing, source));

        const typed_array_destination_test::deep_const_view readonly{backing};
        static_assert(std::same_as<std::ranges::range_reference_t<decltype(readonly)>, const std::int32_t &>);
        static_assert(!std::ranges::contiguous_range<decltype(readonly)>);
        std::vector<std::byte> reencoded;
        REQUIRE(make_encoder<codec::typed_array>(reencoded)(as_typed_array(readonly)));
        std::vector<std::int32_t> copied;
        REQUIRE(make_decoder<codec::typed_array>(reencoded)(as_typed_array(copied)));
        CHECK_EQ(copied, source);
    }

    TEST_CASE("typed array borrowed views preserve mutable range capabilities") {
        check_asymmetric_capability_view<typed_array_byte_order::little>();
        check_asymmetric_capability_view<typed_array_byte_order::big>();
    }

    TEST_CASE("typed array appendable borrowed views preserve destination operations and limits") {
        using view_type = typed_array_destination_test::appendable_view;
        static_assert(std::ranges::contiguous_range<view_type>);
        static_assert(std::ranges::contiguous_range<const view_type>);
        static_assert(std::same_as<std::ranges::range_reference_t<view_type>, std::ranges::range_reference_t<const view_type>>);

        const std::vector<std::int32_t> source{1, -2, 3};
        const auto                      bytes = encoded_values(source);
        std::vector<std::int32_t>       backing;
        REQUIRE(make_decoder<codec::typed_array>(bytes)(as_typed_array(view_type{backing, 3})));
        CHECK_EQ(backing, source);

        backing.assign({91});
        const auto wrapped = as_typed_array(view_type{backing, 2});
        const auto result  = make_decoder<codec::typed_array>(bytes)(as_bounded_size(wrapped, 0, 3));
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(backing, (std::vector<std::int32_t>{91}));
    }

    TEST_CASE("mutable typed array destinations support sized non-contiguous input") {
        const std::vector<std::int32_t> source{1, -2, 3};
        const auto                      bytes = encoded_values<typed_array_byte_order::big>(source);
        const std::deque<std::byte>     input(bytes.begin(), bytes.end());
        std::vector<std::int32_t>       vector_output{99};
        std::array<std::int32_t, 3>     span_output{};

        REQUIRE(make_decoder<codec::typed_array>(input)(as_typed_array_be(vector_output)));
        REQUIRE(make_decoder<codec::typed_array>(input)(as_typed_array_be(std::span{span_output})));
        check_values<std::int32_t>(vector_output, source);
        check_values<std::int32_t>(span_output, source);
    }

    TEST_CASE("mutable typed array destinations accept numeric sequences by capability") {
        check_sequence_destination<std::deque<std::int32_t>>();
        check_sequence_destination<std::list<std::int32_t>>();
        check_sequence_destination<std::forward_list<std::int32_t>>();

        const std::vector<std::int32_t> source{1, -2, 3};
        const auto                      bytes = encoded_values(source);
        std::array<std::int32_t, 3>     fixed{};
        std::int32_t                    builtin[3]{};
        REQUIRE(make_decoder<codec::typed_array>(bytes)(as_typed_array(fixed)));
        REQUIRE(make_decoder<codec::typed_array>(bytes)(as_typed_array(builtin)));
        check_values<std::int32_t>(fixed, source);
        check_values<std::int32_t>(builtin, source);
    }

    TEST_CASE("mutable typed array unsized writable views require the exact element count") {
        const std::vector<std::int32_t> source{1, -2, 3};
        const auto                      bytes = encoded_values(source);
        std::list<std::int32_t>         backing{9, 9, 9};
        auto                            descriptor = std::ranges::subrange(backing.begin(), backing.end());
        static_assert(!std::ranges::sized_range<decltype(descriptor)>);

        REQUIRE(make_decoder<codec::typed_array>(bytes)(as_typed_array(descriptor)));
        CHECK(std::ranges::equal(backing, source));

        for (const auto &mismatched : {std::vector<std::int32_t>{1, 2}, std::vector<std::int32_t>{1, 2, 3, 4}}) {
            backing.assign({91, 92, 93});
            const auto mismatched_bytes = encoded_values(mismatched);
            auto       destination      = std::ranges::subrange(backing.begin(), backing.end());
            const auto result           = make_decoder<codec::typed_array>(mismatched_bytes)(as_typed_array(destination));

            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), status_code::unexpected_group_size);
            CHECK(std::ranges::equal(backing, std::array<std::int32_t, 3>{91, 92, 93}));
        }
    }

    TEST_CASE("typed array wrappers encode unsized numeric source ranges") {
        const std::forward_list<std::int32_t> source{1, -2, 3};
        std::vector<std::byte>                bytes;
        REQUIRE(make_encoder<codec::typed_array>(bytes)(as_typed_array(source)));

        std::vector<std::int32_t> output;
        REQUIRE(make_decoder<codec::typed_array>(bytes)(as_typed_array(output)));
        CHECK_EQ(output, (std::vector<std::int32_t>{1, -2, 3}));

        auto mutable_source = source;
        bytes.clear();
        REQUIRE(make_encoder<codec::typed_array>(bytes)(as_bounded_size<1, 3>(as_typed_array_be(mutable_source))));
        REQUIRE(make_decoder<codec::typed_array>(bytes)(as_typed_array_be(output)));
        CHECK_EQ(output, (std::vector<std::int32_t>{1, -2, 3}));
    }

#ifdef CBOR_TAGS_TEST_TYPED_ARRAY_BOOST
    TEST_CASE("mutable typed array destinations retain small vector inline storage") {
        const std::vector<std::int32_t>                 source{1, -2, 3};
        const auto                                      bytes = encoded_values(source);
        boost::container::small_vector<std::int32_t, 8> output{99};
        const auto                                     *inline_data = output.data();
        auto                                            dec         = make_decoder<codec::typed_array>(bytes);
        auto                                            result      = expected<void, status_code>{};
        {
            allocation_failure_guard guard;
            result = dec(as_bounded_size(as_typed_array(output), 0, 8));
        }

        REQUIRE(result);
        CHECK(std::ranges::equal(output, source));
        CHECK_EQ(output.data(), inline_data);
        check_sequence_destination<boost::container::small_vector<std::int32_t, 8>>();
        check_sequence_destination<boost::container::static_vector<std::int32_t, 8>>();
    }
#endif

    TEST_CASE("mutable typed array wrappers keep readonly annotations and native segmented encoding usable") {
        std::vector<std::int32_t>                                  source{1, -2, 3};
        typed_array_ref<std::int32_t>                              annotation    = as_typed_array(source);
        typed_array_ref<std::int32_t, typed_array_byte_order::big> be_annotation = as_typed_array_be(std::span{source});
        CHECK_EQ(annotation.values().data(), source.data());
        CHECK_EQ(be_annotation.values().data(), source.data());

        const auto             encoded = encoded_values(source);
        std::vector<std::byte> annotated_bytes;
        REQUIRE(make_encoder<codec::typed_array>(annotated_bytes)(annotation));
        CHECK_EQ(annotated_bytes, encoded);

        if constexpr (std::endian::native == std::endian::little) {
            CHECK_EQ(flatten_segments(encode_segments(as_typed_array(source))), encoded);
            CHECK_EQ(flatten_segments(encode_segments(as_typed_array(std::span{source}))), encoded);
        } else {
            const auto be_encoded = encoded_values<typed_array_byte_order::big>(source);
            CHECK_EQ(flatten_segments(encode_segments(as_typed_array_be(source))), be_encoded);
            CHECK_EQ(flatten_segments(encode_segments(as_typed_array_be(std::span{source}))), be_encoded);
        }
    }

    TEST_CASE("mutable typed array destinations render tag and element bounds in cddl") {
        CHECK_EQ(schema<vector_destination>(), "root = #6.78(bstr)");
        CHECK_EQ((schema<bounded_size<vector_destination, 1, 3>>()), "root = #6.78(bstr .size (4..12))");
        CHECK_EQ(schema<span_destination>(), "root = #6.78(bstr)");
        using be_destination = decltype(as_typed_array_be(std::declval<std::vector<std::int16_t> &>()));
        CHECK_EQ((schema<bounded_size<be_destination, 2, 2>>()), "root = #6.73(bstr .size 4)");
    }

} // TEST_SUITE
