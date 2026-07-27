#include "test_util.h"

#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <memory_resource>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace cbor::tags;
using namespace std::string_view_literals;

namespace {
struct minimal_decoder_options {
    using is_options  = void;
    using return_type = expected<void, status_code>;
    using error_type  = status_code;

    static constexpr bool wrap_groups = true;
};

static_assert(IsOptions<minimal_decoder_options>);
static_assert(!cbor::tags::detail::strict_integer_decode_option_v<minimal_decoder_options>);
static_assert(cbor::tags::detail::input_output_alias_check_option_v<minimal_decoder_options>);
static_assert(cbor::tags::detail::input_output_alias_check_option_v<default_options>);
static_assert(!cbor::tags::detail::input_output_alias_check_option_v<unchecked_aliasing_decoder_options>);

struct NonDefaultComparator {
    int tag;

    explicit constexpr NonDefaultComparator(int tag_) : tag(tag_) {}
    NonDefaultComparator() = delete;

    constexpr bool operator()(int lhs, int rhs) const { return lhs < rhs; }
};

struct NonAssignableComparator {
    int tag;

    explicit constexpr NonAssignableComparator(int tag_) : tag(tag_) {}
    NonAssignableComparator()                                                     = delete;
    constexpr NonAssignableComparator(const NonAssignableComparator &)            = default;
    constexpr NonAssignableComparator(NonAssignableComparator &&)                 = default;
    constexpr NonAssignableComparator &operator=(const NonAssignableComparator &) = delete;
    constexpr NonAssignableComparator &operator=(NonAssignableComparator &&)      = delete;

    constexpr bool operator()(int lhs, int rhs) const { return lhs < rhs; }
};

template <typename T> struct NonDefaultAllocator {
    using value_type = T;

    int tag;

    explicit constexpr NonDefaultAllocator(int tag_) noexcept : tag(tag_) {}
    NonDefaultAllocator() = delete;

    template <typename U> constexpr NonDefaultAllocator(const NonDefaultAllocator<U> &other) noexcept : tag(other.tag) {}

    [[nodiscard]] constexpr T *allocate(std::size_t count) { return std::allocator<T>{}.allocate(count); }

    constexpr void deallocate(T *ptr, std::size_t count) noexcept { std::allocator<T>{}.deallocate(ptr, count); }

    template <typename U> constexpr bool operator==(const NonDefaultAllocator<U> &other) const noexcept { return tag == other.tag; }
};

struct CustomSizeByteBuffer {
    using value_type = std::byte;
    using size_type  = std::uint16_t;

    std::vector<std::byte> bytes;

    [[nodiscard]] auto begin() noexcept { return bytes.begin(); }
    [[nodiscard]] auto begin() const noexcept { return bytes.begin(); }
    [[nodiscard]] auto end() noexcept { return bytes.end(); }
    [[nodiscard]] auto end() const noexcept { return bytes.end(); }
    [[nodiscard]] auto data() noexcept { return bytes.data(); }
    [[nodiscard]] auto data() const noexcept { return bytes.data(); }
    [[nodiscard]] auto size() const noexcept { return static_cast<size_type>(bytes.size()); }
};

struct CountingSizeByteBuffer {
    using value_type = std::byte;
    using size_type  = std::size_t;

    std::vector<std::byte> bytes;
    mutable size_type      size_calls{};

    [[nodiscard]] auto begin() const noexcept { return bytes.begin(); }
    [[nodiscard]] auto end() const noexcept { return bytes.end(); }
    [[nodiscard]] auto data() const noexcept { return bytes.data(); }
    [[nodiscard]] auto size() const noexcept {
        ++size_calls;
        return bytes.size();
    }
};

struct AdlSizedByteRange {
    std::array<std::byte, 4> bytes{};

    friend std::byte       *begin(AdlSizedByteRange &range) noexcept { return range.bytes.data(); }
    friend const std::byte *begin(const AdlSizedByteRange &range) noexcept { return range.bytes.data(); }
    friend std::byte       *end(AdlSizedByteRange &range) noexcept { return range.bytes.data() + range.bytes.size(); }
    friend const std::byte *end(const AdlSizedByteRange &range) noexcept { return range.bytes.data() + range.bytes.size(); }
    friend std::uint16_t    size(const AdlSizedByteRange &range) noexcept { return static_cast<std::uint16_t>(range.bytes.size()); }
};

struct SignedSizeDequeByteRange {
    using value_type = std::byte;
    using size_type  = int;

    std::deque<std::byte> bytes;

    [[nodiscard]] auto begin() noexcept { return bytes.begin(); }
    [[nodiscard]] auto begin() const noexcept { return bytes.begin(); }
    [[nodiscard]] auto end() noexcept { return bytes.end(); }
    [[nodiscard]] auto end() const noexcept { return bytes.end(); }
    [[nodiscard]] auto size() const noexcept { return static_cast<size_type>(bytes.size()); }
};

struct CountingUnsizedByteRange {
    using value_type = std::byte;

    struct iterator {
        using value_type        = std::byte;
        using difference_type   = std::ptrdiff_t;
        using iterator_concept  = std::bidirectional_iterator_tag;
        using iterator_category = std::bidirectional_iterator_tag;

        std::list<std::byte>::const_iterator current{};
        std::size_t                         *increments{};

        [[nodiscard]] const std::byte &operator*() const noexcept { return *current; }
        [[nodiscard]] const std::byte *operator->() const noexcept { return std::addressof(*current); }

        iterator &operator++() noexcept {
            ++current;
            ++*increments;
            return *this;
        }
        iterator operator++(int) noexcept {
            auto copy = *this;
            ++*this;
            return copy;
        }
        iterator &operator--() noexcept {
            --current;
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

    [[nodiscard]] iterator begin() const noexcept { return {bytes.cbegin(), &increments}; }
    [[nodiscard]] iterator end() const noexcept { return {bytes.cend(), &increments}; }
};

struct ReserveTrackingIntVector : std::vector<int> {
    using base_type = std::vector<int>;
    using size_type = typename base_type::size_type;
    using base_type::base_type;

    size_type reserve_calls{};
    size_type last_reserve{};

    void reserve(size_type count) {
        ++reserve_calls;
        last_reserve = count;
    }
};

struct ReserveTrackingIntMap : std::unordered_map<int, int> {
    using base_type = std::unordered_map<int, int>;
    using size_type = typename base_type::size_type;
    using base_type::base_type;

    size_type reserve_calls{};
    size_type last_reserve{};

    void reserve(size_type count) {
        ++reserve_calls;
        last_reserve = count;
    }
};

struct ReserveWithoutSizeByteBuffer {
    using value_type = std::byte;
    using size_type  = std::size_t;

    std::vector<std::byte> bytes;

    [[nodiscard]] auto begin() noexcept { return bytes.begin(); }
    [[nodiscard]] auto begin() const noexcept { return bytes.begin(); }
    [[nodiscard]] auto end() noexcept { return bytes.end(); }
    [[nodiscard]] auto end() const noexcept { return bytes.end(); }
    void               reserve(size_type count) { bytes.reserve(count); }
    void               push_back(value_type value) { bytes.push_back(value); }
};

struct ThrowingReservableByteBuffer {
    using value_type = std::byte;
    using size_type  = std::size_t;

    std::vector<std::byte> bytes;
    size_type              appends_before_failure{std::numeric_limits<size_type>::max()};

    [[nodiscard]] auto begin() noexcept { return bytes.begin(); }
    [[nodiscard]] auto begin() const noexcept { return bytes.begin(); }
    [[nodiscard]] auto end() noexcept { return bytes.end(); }
    [[nodiscard]] auto end() const noexcept { return bytes.end(); }
    [[nodiscard]] auto size() const noexcept { return bytes.size(); }
    [[nodiscard]] auto max_size() const noexcept { return bytes.max_size(); }
    void               reserve(size_type count) { bytes.reserve(count); }
    void               push_back(value_type value) {
        if (appends_before_failure == 0) {
            throw std::bad_alloc{};
        }
        --appends_before_failure;
        bytes.push_back(value);
    }
    void insert(std::vector<std::byte>::iterator, const std::byte *first, const std::byte *last) {
        for (; first != last; ++first) {
            push_back(*first);
        }
    }
};

struct ExternalByteBuffer {
    using value_type = std::byte;
    using size_type  = std::size_t;

    std::byte *storage{};
    size_type  current_size{};
    size_type  capacity{};

    [[nodiscard]] auto begin() noexcept { return storage; }
    [[nodiscard]] auto begin() const noexcept { return static_cast<const std::byte *>(storage); }
    [[nodiscard]] auto end() noexcept { return storage + current_size; }
    [[nodiscard]] auto end() const noexcept { return static_cast<const std::byte *>(storage) + current_size; }
    [[nodiscard]] auto size() const noexcept { return current_size; }
    void               push_back(value_type value) {
        if (current_size == capacity) {
            throw std::length_error("external byte buffer is full");
        }
        storage[current_size++] = value;
    }
    void insert(std::byte *, const std::byte *first, const std::byte *last) {
        for (; first != last; ++first) {
            push_back(*first);
        }
    }
    void pop_back() { --current_size; }
};

struct controlled_memory_resource : std::pmr::memory_resource {
    std::pmr::memory_resource *upstream{std::pmr::new_delete_resource()};
    std::size_t                allocations_before_failure{std::numeric_limits<std::size_t>::max()};

  private:
    void *do_allocate(std::size_t bytes, std::size_t alignment) override {
        if (allocations_before_failure == 0) {
            throw std::bad_alloc{};
        }
        --allocations_before_failure;
        return upstream->allocate(bytes, alignment);
    }

    void do_deallocate(void *ptr, std::size_t bytes, std::size_t alignment) override { upstream->deallocate(ptr, bytes, alignment); }

    bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override { return this == &other; }
};
} // namespace

static_assert(!std::is_default_constructible_v<NonDefaultComparator>);
static_assert(std::is_move_assignable_v<NonDefaultComparator>);
static_assert(!std::is_default_constructible_v<NonAssignableComparator>);
static_assert(!std::is_move_assignable_v<NonAssignableComparator>);
static_assert(!std::is_default_constructible_v<NonDefaultAllocator<int>>);
static_assert(CborInputBuffer<CustomSizeByteBuffer>);
static_assert(
    std::same_as<typename decltype(make_decoder(std::declval<CustomSizeByteBuffer &>()))::size_type, CustomSizeByteBuffer::size_type>);
static_assert(CborInputBuffer<CountingSizeByteBuffer>);
static_assert(CborInputBuffer<CountingUnsizedByteRange>);
static_assert(!std::ranges::sized_range<const CountingUnsizedByteRange>);
static_assert(IsRangeOfCborValues<ReserveTrackingIntVector>);
static_assert(IsMap<ReserveTrackingIntMap>);
static_assert(CborInputBuffer<AdlSizedByteRange>);
static_assert(std::same_as<typename decltype(make_decoder(std::declval<AdlSizedByteRange &>()))::size_type, std::uint16_t>);
static_assert(CborInputBuffer<SignedSizeDequeByteRange>);
static_assert(std::same_as<typename decltype(make_decoder(std::declval<SignedSizeDequeByteRange &>()))::size_type, int>);
static_assert(IsBinaryString<ReserveWithoutSizeByteBuffer>);
static_assert(HasReserve<ReserveWithoutSizeByteBuffer>);
static_assert(!detail::AppendReservable<ReserveWithoutSizeByteBuffer>);
static_assert(IsBinaryString<ThrowingReservableByteBuffer>);
static_assert(detail::AppendReservable<ThrowingReservableByteBuffer>);
static_assert(IsBinaryString<ExternalByteBuffer>);

TEST_CASE("float16 should cover infinity nan and subnormal conversions") {
    CHECK(std::isinf(static_cast<float>(float16_t{static_cast<std::uint16_t>(0x7C00)})));
    CHECK(std::isnan(static_cast<float>(float16_t{static_cast<std::uint16_t>(0x7E00)})));
    CHECK_EQ(static_cast<float>(float16_t{static_cast<std::uint16_t>(0x0001)}), std::ldexp(1.0F, -24));
    CHECK(std::signbit(static_cast<float>(float16_t{static_cast<std::uint16_t>(0x8000)})));

    float16_t underflow{std::numeric_limits<float>::denorm_min()};
    CHECK_EQ(underflow.value, 0);

    CHECK_EQ(float16_t{std::ldexp(1.0F, -24)}.value, std::uint16_t{0x0001});
    CHECK_EQ(float16_t{std::ldexp(1.0, -24)}.value, std::uint16_t{0x0001});
    CHECK_EQ(float16_t{std::ldexp(1023.0F, -24)}.value, std::uint16_t{0x03FF});
    CHECK_EQ(float16_t{std::ldexp(1.0F, -14)}.value, std::uint16_t{0x0400});
    CHECK_EQ(float16_t{1.00048828125F}.value, std::uint16_t{0x3C00});
    CHECK_EQ(float16_t{1.0006F}.value, std::uint16_t{0x3C01});
    CHECK_EQ(float16_t{65504.0F}.value, std::uint16_t{0x7BFF});
    CHECK_EQ(float16_t{65520.0F}.value, std::uint16_t{0x7C00});
}

TEST_CASE("decoder should slice unsigned integer overflow") {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1U));

    auto dec = make_decoder(buffer);

    std::uint32_t decoded{};
    auto          result = dec(decoded);

    REQUIRE(result);
    CHECK_EQ(decoded, 0U);
}

TEST_CASE("decoder should slice signed positive integer overflow") {
    std::vector<std::byte> buffer{std::byte{0x18}, std::byte{0x80}}; // uint(128)

    auto dec = make_decoder(buffer);

    std::int8_t decoded{};
    auto        result = dec(decoded);

    REQUIRE(result);
    CHECK_EQ(decoded, std::numeric_limits<std::int8_t>::min());
}

TEST_CASE("decoder should slice signed negative integer underflow") {
    std::vector<std::byte> buffer{std::byte{0x38}, std::byte{0x80}}; // -129

    auto dec = make_decoder(buffer);

    std::int8_t decoded{};
    auto        result = dec(decoded);

    REQUIRE(result);
    CHECK_EQ(decoded, std::numeric_limits<std::int8_t>::max());
}

TEST_CASE("strict integer decoder option should reject unsigned integer overflow") {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1U));

    auto dec = make_decoder_with_options<strict_integer_decoder_options>(buffer);

    std::uint32_t decoded{};
    auto          result = dec(decoded);

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::no_match_for_uint_on_buffer);
}

TEST_CASE("strict integer decoder option should reject signed positive integer overflow") {
    std::vector<std::byte> buffer{std::byte{0x18}, std::byte{0x80}}; // uint(128)

    auto dec = make_decoder_with_options<strict_integer_decoder_options>(buffer);

    std::int8_t decoded{};
    auto        result = dec(decoded);

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::no_match_for_int_on_buffer);
}

TEST_CASE("strict integer decoder option should reject signed negative integer underflow") {
    std::vector<std::byte> buffer{std::byte{0x38}, std::byte{0x80}}; // -129

    auto dec = make_decoder_with_options<strict_integer_decoder_options>(buffer);

    std::int8_t decoded{};
    auto        result = dec(decoded);

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::no_match_for_int_on_buffer);
}

TEST_CASE("minimal decoder options use default integer slicing") {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1U));

    auto dec = make_decoder_with_options<minimal_decoder_options>(buffer);

    std::uint32_t decoded{};
    auto          result = dec(decoded);

    REQUIRE(result);
    CHECK_EQ(decoded, 0U);
}

TEST_CASE("decoder should accept integer decode boundaries") {
    {
        std::vector<std::byte> buffer{std::byte{0x38}, std::byte{0x7F}}; // -128
        auto                   dec = make_decoder(buffer);
        std::int8_t            decoded{};
        auto                   result = dec(decoded);

        REQUIRE(result);
        CHECK_EQ(decoded, std::numeric_limits<std::int8_t>::min());
    }

    {
        std::vector<std::byte> buffer{std::byte{0x18}, std::byte{0x7F}}; // uint(127)
        auto                   dec = make_decoder(buffer);
        std::int8_t            decoded{};
        auto                   result = dec(decoded);

        REQUIRE(result);
        CHECK_EQ(decoded, std::numeric_limits<std::int8_t>::max());
    }

    {
        std::vector<std::byte> buffer{std::byte{0x18}, std::byte{0xFF}}; // uint(255)
        auto                   dec = make_decoder(buffer);
        std::uint8_t           decoded{};
        auto                   result = dec(decoded);

        REQUIRE(result);
        CHECK_EQ(decoded, std::numeric_limits<std::uint8_t>::max());
    }
}

TEST_CASE("decoder should preserve cbor integer sign") {
    std::vector<std::byte> buffer{std::byte{0x20}}; // -1

    auto dec = make_decoder(buffer);

    integer decoded{0};
    auto    result = dec(decoded);

    REQUIRE(result);
    CHECK(decoded.is_negative);
    CHECK_EQ(decoded.value, 1);
}

TEST_CASE("decoder should represent the CBOR minimum integer with the negative zero sentinel") {
    std::vector<std::byte> buffer{std::byte{0x3B}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
                                  std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};

    {
        auto    dec = make_decoder(buffer);
        integer decoded{0};
        auto    result = dec(decoded);

        REQUIRE(result);
        CHECK(decoded.is_negative);
        CHECK_EQ(decoded.value, 0);
    }

    {
        auto     dec = make_decoder(buffer);
        negative decoded{1};
        auto     result = dec(decoded);

        REQUIRE(result);
        CHECK_EQ(decoded.value, 0);
    }
}

TEST_CASE("decoder should accept largest representable negative wrapper value") {
    std::vector<std::byte> buffer{std::byte{0x3B}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
                                  std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFE}};

    {
        auto    dec = make_decoder(buffer);
        integer decoded{0};
        auto    result = dec(decoded);

        REQUIRE(result);
        CHECK(decoded.is_negative);
        CHECK_EQ(decoded.value, std::numeric_limits<std::uint64_t>::max());
    }

    {
        auto     dec = make_decoder(buffer);
        negative decoded{1};
        auto     result = dec(decoded);

        REQUIRE(result);
        CHECK_EQ(decoded.value, std::numeric_limits<std::uint64_t>::max());
    }
}

TEST_CASE("decoder should reject truncated integer payloads") {
    struct Case {
        std::vector<std::byte> bytes;
    };

    const Case cases[] = {
        {{std::byte{0x18}}},
        {{std::byte{0x19}, std::byte{0x00}}},
        {{std::byte{0x1A}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}}},
        {{std::byte{0x1B}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
          std::byte{0x00}}},
    };

    for (const auto &test_case : cases) {
        auto          dec = make_decoder(test_case.bytes);
        std::uint64_t decoded{};
        auto          result = dec(decoded);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
    }
}

TEST_CASE("decoder should reject truncated float payloads") {
    {
        std::vector<std::byte> buffer{std::byte{0xF9}, std::byte{0x3C}};
        auto                   dec = make_decoder(buffer);
        float16_t              decoded{};
        auto                   result = dec(decoded);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
    }

    {
        std::vector<std::byte> buffer{std::byte{0xFA}, std::byte{0x3F}, std::byte{0x80}, std::byte{0x00}};
        auto                   dec = make_decoder(buffer);
        float                  decoded{};
        auto                   result = dec(decoded);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
    }

    {
        std::vector<std::byte> buffer{std::byte{0xFB}, std::byte{0x3F}, std::byte{0xF0}, std::byte{0x00},
                                      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
        auto                   dec = make_decoder(buffer);
        double                 decoded{};
        auto                   result = dec(decoded);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
    }
}

TEST_CASE("decoder should map invalid additional information to error") {
    {
        std::vector<std::byte> buffer{std::byte{0x1C}};
        auto                   dec = make_decoder(buffer);
        std::uint64_t          decoded{};
        auto                   result = dec(decoded);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
    }

    {
        std::vector<std::byte> buffer{std::byte{0x5C}};
        auto                   dec = make_decoder(buffer);
        std::vector<std::byte> decoded;
        auto                   result = dec(decoded);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
    }
}

TEST_CASE("decoder should reject wrong chunk types inside indefinite strings") {
    {
        std::vector<std::byte> buffer{std::byte{0x5F}, std::byte{0x61}, std::byte{'x'}, std::byte{0xFF}};
        auto                   dec = make_decoder(buffer);
        std::vector<std::byte> decoded;
        auto                   result = dec(as_indefinite{decoded});
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_bstr_on_buffer);
    }

    {
        std::vector<std::byte> buffer{std::byte{0x7F}, std::byte{0x41}, std::byte{0x01}, std::byte{0xFF}};
        auto                   dec = make_decoder(buffer);
        std::string            decoded;
        auto                   result = dec(as_indefinite{decoded});
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_tstr_on_buffer);
    }
}

TEST_CASE("decoder should propagate incomplete from nested indefinite arrays") {
    std::vector<std::byte> buffer{std::byte{0x9F}, std::byte{0x9F}, std::byte{0x01}, std::byte{0xFF}};

    auto                          dec = make_decoder(buffer);
    std::vector<std::vector<int>> decoded;
    auto                          result = dec(as_indefinite{decoded});

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::incomplete);
}

TEST_CASE("decoder should propagate incomplete from variant alternatives") {
    std::vector<std::byte> buffer{std::byte{0x62}, std::byte{'a'}};

    auto                                     dec = make_decoder(buffer);
    std::variant<std::uint64_t, as_text_any> decoded;
    auto                                     result = dec(decoded);

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::incomplete);
}

TEST_CASE("decoder should decode extended simple values") {
    std::vector<std::byte> buffer{std::byte{0xF8}, std::byte{0x10}};

    auto   dec = make_decoder(buffer);
    simple decoded{};
    auto   result = dec(decoded);

    REQUIRE(result);
    CHECK_EQ(decoded.value, 0x10);
}

TEST_CASE("decoder should accept empty byte strings") {
    std::vector<std::byte> buffer{std::byte{0x40}}; // 0-length bstr

    auto dec = make_decoder(buffer);

    std::vector<std::byte> decoded;
    auto                   result = dec(decoded);

    CHECK_MESSAGE(result, "Decoding an empty byte string should succeed.");
    CHECK(decoded.empty());
}

TEST_CASE("decoder should accept empty text strings") {
    std::vector<std::byte> buffer{std::byte{0x60}}; // 0-length tstr

    auto dec = make_decoder(buffer);

    std::string decoded;
    auto        result = dec(decoded);

    CHECK_MESSAGE(result, "Decoding an empty text string should succeed.");
    CHECK(decoded.empty());
}

TEST_CASE("decoder appends definite strings to mutable targets") {
    const std::vector<std::byte> source_bytes{std::byte{0xAA}, std::byte{0xBB}};
    const std::string            source_text{"payload"};
    std::vector<std::byte>       buffer;
    auto                         enc = make_encoder(buffer);
    REQUIRE(enc(source_bytes, source_text, source_text));

    std::vector<std::byte> decoded_bytes{std::byte{0x01}};
    std::string            decoded_text{"prefix:"};
    std::pmr::string       decoded_pmr_text{"pmr:"};
    auto                   dec    = make_decoder(buffer);
    auto                   result = dec(decoded_bytes, decoded_text, decoded_pmr_text);

    REQUIRE(result);
    CHECK_EQ(decoded_bytes, (std::vector<std::byte>{std::byte{0x01}, std::byte{0xAA}, std::byte{0xBB}}));
    CHECK_EQ(decoded_text, "prefix:payload");
    CHECK_EQ(decoded_pmr_text, "pmr:payload");
}

TEST_CASE("decoder appends non-contiguous definite text in one pass") {
    std::deque<std::byte> input{std::byte{0x78}, std::byte{0x40}};
    input.insert(input.end(), 64, std::byte{'x'});

    std::string decoded{"prefix:"};
    auto        dec    = make_decoder(input);
    auto        result = dec(decoded);

    REQUIRE(result);
    CHECK_EQ(decoded, "prefix:" + std::string(64, 'x'));
}

TEST_CASE("decoder leaves mutable strings unchanged for truncated definite payloads") {
    {
        const std::vector<std::byte> buffer{std::byte{0x43}, std::byte{0xAA}};
        std::vector<std::byte>       decoded{std::byte{0x01}};
        auto                         dec    = make_decoder(buffer);
        auto                         result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
        CHECK_EQ(decoded, (std::vector<std::byte>{std::byte{0x01}}));
    }

    {
        const std::vector<std::byte> buffer{std::byte{0x63}, std::byte{'a'}};
        std::string                  decoded{"prefix"};
        auto                         dec    = make_decoder(buffer);
        auto                         result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
        CHECK_EQ(decoded, "prefix");
    }
}

TEST_CASE("decoder validates definite string payloads once") {
    SUBCASE("byte string") {
        CountingSizeByteBuffer input{{std::byte{0x42}, std::byte{0x01}, std::byte{0x02}}};
        std::vector<std::byte> decoded;
        auto                   dec = make_decoder(input);

        REQUIRE(dec(decoded));
        CHECK_EQ(decoded, (std::vector{std::byte{0x01}, std::byte{0x02}}));
        // Initial-byte, payload, and alias checks may each query the input size.
        CHECK(input.size_calls <= 3);
    }

    SUBCASE("text string") {
        CountingSizeByteBuffer input{{std::byte{0x62}, std::byte{'o'}, std::byte{'k'}}};
        std::string            decoded;
        auto                   dec = make_decoder(input);

        REQUIRE(dec(decoded));
        CHECK_EQ(decoded, "ok");
        // Initial-byte, payload, and alias checks may each query the input size.
        CHECK(input.size_calls <= 3);
    }
}

TEST_CASE("decoder consumes unsized string headers in one destructive traversal") {
    SUBCASE("byte string header") {
        CountingUnsizedByteRange input{{std::byte{0x43}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}}};
        as_bstr_any              decoded{};
        auto                     dec = make_decoder(input);

        REQUIRE(dec(decoded));
        CHECK_EQ(decoded.size, 3);
        CHECK_EQ(input.increments, input.bytes.size());
        CHECK(dec.tell() == input.end());
    }

    SUBCASE("text string header") {
        CountingUnsizedByteRange input{{std::byte{0x62}, std::byte{'o'}, std::byte{'k'}}};
        as_text_any              decoded{};
        auto                     dec = make_decoder(input);

        REQUIRE(dec(decoded));
        CHECK_EQ(decoded.size, 2);
        CHECK_EQ(input.increments, input.bytes.size());
        CHECK(dec.tell() == input.end());
    }
}

TEST_CASE("decoder consumes unsized indefinite string chunks in one traversal") {
    SUBCASE("byte string") {
        CountingUnsizedByteRange input{
            {std::byte{0x5F}, std::byte{0x43}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0xFF}}};
        std::vector<std::byte> decoded;
        auto                   dec = make_decoder(input);

        REQUIRE(dec(decoded));
        CHECK_EQ(decoded, (std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}}));
        CHECK_EQ(input.increments, input.bytes.size());
        CHECK(dec.tell() == input.end());
    }

    SUBCASE("text string") {
        CountingUnsizedByteRange input{{std::byte{0x7F}, std::byte{0x62}, std::byte{'o'}, std::byte{'k'}, std::byte{0xFF}}};
        std::string              decoded;
        auto                     dec = make_decoder(input);

        REQUIRE(dec(decoded));
        CHECK_EQ(decoded, "ok");
        CHECK_EQ(input.increments, input.bytes.size());
        CHECK(dec.tell() == input.end());
    }
}

TEST_CASE("decoder retains unsized indefinite string chunk prefixes after incomplete input") {
    SUBCASE("byte string") {
        CountingUnsizedByteRange input{{std::byte{0x5F}, std::byte{0x43}, std::byte{0x01}, std::byte{0x02}}};
        std::vector<std::byte>   decoded{std::byte{0x99}};
        auto                     dec = make_decoder(input);

        const auto result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
        CHECK_EQ(decoded, (std::vector<std::byte>{std::byte{0x99}, std::byte{0x01}, std::byte{0x02}}));
        CHECK_EQ(input.increments, input.bytes.size());
        CHECK(dec.tell() == input.end());
    }

    SUBCASE("text string") {
        CountingUnsizedByteRange input{{std::byte{0x7F}, std::byte{0x62}, std::byte{'o'}}};
        std::string              decoded{"prefix:"};
        auto                     dec = make_decoder(input);

        const auto result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
        CHECK_EQ(decoded, "prefix:o");
        CHECK_EQ(input.increments, input.bytes.size());
        CHECK(dec.tell() == input.end());
    }
}

TEST_CASE("decoder reserves definite containers only after a size confirmation") {
    SUBCASE("confirmed sized array keeps the reserve fast path") {
        const std::vector<std::byte> input{std::byte{0x82}, std::byte{0x01}, std::byte{0x02}};
        ReserveTrackingIntVector     decoded;
        auto                         dec = make_decoder(input);

        REQUIRE(dec(decoded));
        CHECK_EQ(decoded.reserve_calls, 1U);
        CHECK_EQ(decoded.last_reserve, 2U);
        CHECK_EQ(std::vector<int>(decoded.begin(), decoded.end()), (std::vector<int>{1, 2}));
    }

    SUBCASE("truncated sized array cannot force a reserve") {
        const std::vector<std::byte> input{std::byte{0x9A}, std::byte{0x00}, std::byte{0x0F}, std::byte{0x42}, std::byte{0x40}};
        ReserveTrackingIntVector     decoded;
        auto                         dec = make_decoder(input);

        const auto result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
        CHECK_EQ(decoded.reserve_calls, 0U);
        CHECK(decoded.empty());
    }

    SUBCASE("truncated sized map uses a two-item lower bound per pair") {
        const std::vector<std::byte> input{std::byte{0xA2}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
        ReserveTrackingIntMap        decoded;
        auto                         dec = make_decoder(input);

        const auto result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
        CHECK_EQ(decoded.reserve_calls, 0U);
        CHECK(decoded.empty());
    }

    SUBCASE("unsized input commits a prefix without reservation") {
        CountingUnsizedByteRange input{{std::byte{0x84}, std::byte{0x01}, std::byte{0x02}}};
        ReserveTrackingIntVector decoded;
        auto                     dec = make_decoder(input);

        const auto result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
        CHECK_EQ(decoded.reserve_calls, 0U);
        CHECK_EQ(std::vector<int>(decoded.begin(), decoded.end()), (std::vector<int>{1, 2}));
        CHECK_EQ(input.increments, input.bytes.size());
    }

    SUBCASE("unsized map input commits complete pairs without reservation") {
        CountingUnsizedByteRange input{{std::byte{0xA2}, std::byte{0x01}, std::byte{0x02}}};
        ReserveTrackingIntMap    decoded;
        auto                     dec = make_decoder(input);

        const auto result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
        CHECK_EQ(decoded.reserve_calls, 0U);
        CHECK_EQ(decoded.size(), 1U);
        CHECK_EQ(decoded.at(1), 2);
        CHECK_EQ(input.increments, input.bytes.size());
    }
}

TEST_CASE("decoder retains unsized string prefixes after incomplete input") {
    SUBCASE("header skip is destructive") {
        CountingUnsizedByteRange input{{std::byte{0x45}, std::byte{0x01}, std::byte{0x02}}};
        as_bstr_any              decoded{};
        auto                     dec = make_decoder(input);

        auto result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
        CHECK_EQ(decoded.size, 5);
        CHECK_EQ(input.increments, input.bytes.size());
        CHECK(dec.tell() == input.end());
    }

    SUBCASE("owning text string retains the consumed payload prefix") {
        CountingUnsizedByteRange input{{std::byte{0x65}, std::byte{'o'}, std::byte{'k'}}};
        std::string              decoded{"prefix:"};
        auto                     dec = make_decoder(input);

        auto result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
        CHECK_EQ(decoded, "prefix:ok");
        CHECK_EQ(input.increments, input.bytes.size());
        CHECK(dec.tell() == input.end());
    }
}

TEST_CASE("decoder does not roll back incomplete unsized indefinite elements") {
    CountingUnsizedByteRange input{{std::byte{0x9F}, std::byte{0x18}}};
    std::vector<int>         decoded;
    auto                     dec = make_decoder(input);

    auto result = dec(decoded);

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::incomplete);
    CHECK(decoded.empty());
    CHECK_EQ(input.increments, input.bytes.size());
    CHECK(dec.tell() == input.end());
}

TEST_CASE("decoder rejects definite string targets that alias input storage") {
    SUBCASE("same byte vector") {
        std::vector<std::byte> input{std::byte{0x43}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
        const auto             original = input;
        auto                   dec      = make_decoder(input);
        auto                   result   = dec(input);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK_EQ(input, original);
    }

    SUBCASE("same text string") {
        std::string input{"\x63"
                          "abc",
                          4};
        const auto  original = input;
        auto        dec      = make_decoder(input);
        auto        result   = dec(input);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK_EQ(input, original);
    }

    SUBCASE("input span overlaps byte vector") {
        std::vector<std::byte> storage{std::byte{0x43}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
        const auto             original = storage;
        const auto             input    = std::span<const std::byte>{storage};
        auto                   dec      = make_decoder(input);
        auto                   result   = dec(storage);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK_EQ(storage, original);
    }

    SUBCASE("input span starts inside byte vector") {
        std::vector<std::byte> storage{std::byte{0x00}, std::byte{0x43}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
        const auto             original = storage;
        const auto             input    = std::span<const std::byte>{storage}.subspan(1);
        auto                   dec      = make_decoder(input);
        auto                   result   = dec(storage);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK_EQ(storage, original);
    }

    SUBCASE("mutable target starts inside input") {
        std::vector<std::byte> input{std::byte{0x41}, std::byte{0xAA}, std::byte{0x00}};
        const auto             original = input;
        ExternalByteBuffer     target{input.data() + 1, 1, 2};
        auto                   dec    = make_decoder(input);
        auto                   result = dec(target);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK_EQ(input, original);
    }

    SUBCASE("adjacent input and target ranges do not overlap") {
        std::vector<std::byte> storage{std::byte{0x41}, std::byte{0xAA}, std::byte{0x00}, std::byte{0x00}};
        const auto             input = std::span<const std::byte>{storage}.first<2>();
        ExternalByteBuffer     target{storage.data() + 2, 1, 2};
        auto                   dec = make_decoder(input);

        REQUIRE(dec(target));
        REQUIRE_EQ(target.size(), 2);
        CHECK_EQ(target.storage[1], std::byte{0xAA});
    }
}

TEST_CASE("decoder rejects indefinite string targets that alias input storage") {
    SUBCASE("byte string") {
        std::vector<std::byte> storage{
            std::byte{0x5F}, std::byte{0x42}, std::byte{0x01}, std::byte{0x02}, std::byte{0xFF},
        };
        const auto original = storage;
        const auto input    = std::span<const std::byte>{storage};
        auto       dec      = make_decoder(input);
        auto       result   = dec(storage);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK_EQ(storage, original);
    }

    SUBCASE("text string") {
        std::string storage{
            static_cast<char>(0x7F), static_cast<char>(0x62), 'o', 'k', static_cast<char>(0xFF),
        };
        const auto original = storage;
        const auto input    = std::as_bytes(std::span<const char>{storage.data(), storage.size()});
        auto       dec      = make_decoder(input);
        auto       result   = dec(storage);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK_EQ(storage, original);
    }
}

TEST_CASE("decoder alias checks can be disabled through compile-time options") {
    SUBCASE("definite byte string") {
        std::array storage{
            std::byte{0x42}, std::byte{0x01}, std::byte{0x02}, std::byte{0x00}, std::byte{0x00},
        };
        const auto         input = std::span<const std::byte>{storage}.first<3>();
        ExternalByteBuffer target{storage.data(), 3, storage.size()};
        auto               dec = make_decoder_with_options<unchecked_aliasing_decoder_options>(input);

        REQUIRE(dec(target));
        REQUIRE_EQ(target.size(), 5);
        CHECK_EQ(storage[3], std::byte{0x01});
        CHECK_EQ(storage[4], std::byte{0x02});
    }

    SUBCASE("indefinite byte string") {
        std::array storage{
            std::byte{0x5F}, std::byte{0x42}, std::byte{0x01}, std::byte{0x02}, std::byte{0xFF}, std::byte{0x00}, std::byte{0x00},
        };
        const auto         input = std::span<const std::byte>{storage}.first<5>();
        ExternalByteBuffer target{storage.data(), 5, storage.size()};
        auto               dec = make_decoder_with_options<unchecked_aliasing_decoder_options>(input);

        REQUIRE(dec(target));
        REQUIRE_EQ(target.size(), 7);
        CHECK_EQ(storage[5], std::byte{0x01});
        CHECK_EQ(storage[6], std::byte{0x02});
    }

    SUBCASE("fixed byte string") {
        struct overlapping_input {
            std::array<std::byte, 3> decoded;
            std::byte                tail;
        };
        static_assert(offsetof(overlapping_input, tail) == 3);
        static_assert(sizeof(overlapping_input) == 4);

        overlapping_input storage{
            .decoded = {std::byte{0x43}, std::byte{0x01}, std::byte{0x02}},
            .tail    = std::byte{0x03},
        };
        const auto input = std::as_bytes(std::span{&storage, std::size_t{1}});
        auto       dec   = make_decoder_with_options<unchecked_aliasing_decoder_options>(input);

        REQUIRE(dec(storage.decoded));
        CHECK_EQ(storage.decoded, (std::array{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}}));
    }
}

TEST_CASE("decoder rejects fixed byte string targets that alias input") {
    struct overlapping_input {
        std::array<std::byte, 2> prefix;
        std::array<std::byte, 2> decoded;
    };
    static_assert(offsetof(overlapping_input, decoded) == 2);
    static_assert(sizeof(overlapping_input) == 4);

    overlapping_input storage{
        .prefix  = {std::byte{0x42}, std::byte{0x01}},
        .decoded = {std::byte{0x02}, std::byte{0x01}},
    };
    const auto original = storage;
    const auto input    = std::as_bytes(std::span{&storage, std::size_t{1}});
    auto       dec      = make_decoder(input);
    auto       result   = dec(storage.decoded);

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::error);
    CHECK_EQ(storage.prefix, original.prefix);
    CHECK_EQ(storage.decoded, original.decoded);
}

TEST_CASE("decoder reserves non-contiguous definite text before mutation") {
    std::deque<std::byte> input{std::byte{0x78}, std::byte{0x40}};
    input.insert(input.end(), 64, std::byte{'x'});

    controlled_memory_resource resource;
    std::pmr::string           decoded{"prefix", &resource};
    const auto                 original = std::string{decoded};
    resource.allocations_before_failure = 0;

    auto dec    = make_decoder(input);
    auto result = dec(decoded);

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::out_of_memory);
    CHECK_EQ(std::string_view{decoded}, original);
}

TEST_CASE("decoder does not roll back non-reservable string appends when the destination fails") {
    const std::vector<std::byte> input{std::byte{0x43}, std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}};
    std::array<std::byte, 2>     storage{std::byte{0x01}, std::byte{0x00}};
    ExternalByteBuffer           decoded{storage.data(), 1, storage.size()};

    auto dec    = make_decoder(input);
    auto result = dec(decoded);

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::out_of_memory);
    REQUIRE_EQ(decoded.size(), 2);
    CHECK_EQ(storage[0], std::byte{0x01});
    CHECK_EQ(storage[1], std::byte{0xAA});
}

TEST_CASE("decoder does not roll back reservable string appends when the destination fails") {
    const std::vector<std::byte> input{std::byte{0x43}, std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}};
    ThrowingReservableByteBuffer decoded{{std::byte{0x01}}, 1};

    auto dec    = make_decoder(input);
    auto result = dec(decoded);

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::out_of_memory);
    CHECK_EQ(decoded.bytes, (std::vector<std::byte>{std::byte{0x01}, std::byte{0xAA}}));
}

TEST_CASE("reserve without size falls back to direct append") {
    const std::deque<std::byte>  input{std::byte{0x43}, std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}};
    ReserveWithoutSizeByteBuffer decoded{{std::byte{0x01}}};

    auto dec    = make_decoder(input);
    auto result = dec(decoded);

    REQUIRE(result);
    CHECK_EQ(decoded.bytes, (std::vector<std::byte>{std::byte{0x01}, std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}}));
}

TEST_CASE("decoder should preserve text bytes without utf8 validation") {
    // tstr(2): 0xC3 0x28 is an invalid UTF-8 sequence.
    std::vector<std::byte> buffer{std::byte{0x62}, std::byte{0xC3}, std::byte{0x28}, std::byte{0x01}};

    auto dec = make_decoder(buffer);

    std::string  decoded;
    std::uint8_t next_value{};
    auto         result = dec(decoded, next_value);

    std::string expected;
    expected.push_back(static_cast<char>(0xC3));
    expected.push_back(static_cast<char>(0x28));

    CHECK_MESSAGE(result, "Core text decode preserves bytes and does not validate UTF-8.");
    CHECK_EQ(decoded, expected);
    CHECK_EQ(next_value, 1);
}

TEST_CASE("decoder should reject undersized byte strings for fixed arrays") {
    std::vector<std::byte> buffer{std::byte{0x41}, std::byte{0x01}}; // length 1, value 0x01

    auto dec = make_decoder(buffer);

    std::array<std::byte, 2> decoded{};
    decoded.fill(std::byte{0xAA});

    auto result = dec(decoded);

    CHECK_FALSE_MESSAGE(result, "Decoding into a larger fixed array should flag the size mismatch.");
    CHECK_EQ(result.error(), status_code::unexpected_group_size);
    CHECK_EQ(decoded[0], std::byte{0xAA});
    CHECK_EQ(decoded[1], std::byte{0xAA});
}

TEST_CASE("decoder should decode byte strings into basic_string_view and advance") {
    // bstr(2): 0x42 0x01 0x02, then uint(1): 0x01
    std::vector<std::byte> buffer{std::byte{0x42}, std::byte{0x01}, std::byte{0x02}, std::byte{0x01}};

    auto dec = make_decoder(buffer);

    std::basic_string_view<std::byte> decoded;
    std::uint8_t                      next_value{};
    auto                              result = dec(decoded, next_value);

    CHECK_MESSAGE(result, "Decoding into a byte-string view should advance the reader.");
    CHECK_EQ(decoded.size(), 2);
    CHECK_EQ(decoded[0], std::byte{0x01});
    CHECK_EQ(decoded[1], std::byte{0x02});
    CHECK_EQ(next_value, 1);
}

TEST_CASE("decoder should accept empty byte strings for basic_string_view and advance") {
    // empty bstr: 0x40, then uint(1): 0x01
    std::vector<std::byte> buffer{std::byte{0x40}, std::byte{0x01}};

    auto dec = make_decoder(buffer);

    std::basic_string_view<std::byte> decoded;
    std::uint8_t                      next_value{};
    auto                              result = dec(decoded, next_value);

    CHECK_MESSAGE(result, "Decoding an empty byte-string view should succeed and advance the reader.");
    CHECK(decoded.empty());
    CHECK_EQ(next_value, 1);
}

TEST_CASE("decoder should accept empty byte strings for basic_string_view at end-of-buffer") {
    // empty bstr: 0x40
    // Regression: previously could form `&data_[pos]` out of bounds (ASAN).
    std::vector<std::byte> buffer{std::byte{0x40}};

    auto dec = make_decoder(buffer);

    std::basic_string_view<std::byte> decoded;
    auto                              result = dec(decoded);

    CHECK_MESSAGE(result, "Decoding an empty byte-string view should succeed without touching payload bytes.");
    CHECK(decoded.empty());
}

TEST_CASE("decoder should preserve custom input buffer size_type") {
    CustomSizeByteBuffer buffer{.bytes = {std::byte{0x42}, std::byte{0x01}, std::byte{0x02}, std::byte{0x01}}};

    auto dec = make_decoder(buffer);

    std::basic_string_view<std::byte> decoded;
    std::uint8_t                      next_value{};
    auto                              result = dec(decoded, next_value);

    REQUIRE(result);
    CHECK_EQ(decoded.size(), 2);
    CHECK_EQ(decoded[0], std::byte{0x01});
    CHECK_EQ(decoded[1], std::byte{0x02});
    CHECK_EQ(next_value, 1);
}

TEST_CASE("decoder tell supports ADL-only ranges") {
    AdlSizedByteRange buffer{.bytes = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}}};

    auto dec = make_decoder(buffer);
    CHECK(dec.tell() == std::ranges::begin(buffer));

    std::uint8_t value{};
    auto         result = dec(value);

    REQUIRE(result);
    CHECK_EQ(value, 1);
    CHECK(dec.tell() == std::ranges::begin(buffer) + 1);
}

TEST_CASE("decoder readers reject negative seeks before begin") {
    std::vector<std::byte>                             contiguous{std::byte{0x01}};
    cbor::tags::detail::reader<std::vector<std::byte>> contiguous_reader{contiguous};
    CHECK_THROWS_AS(contiguous_reader.seek(-1), std::runtime_error);
    CHECK_THROWS_AS(contiguous_reader.seek(std::numeric_limits<std::ptrdiff_t>::min()), std::runtime_error);

    std::deque<std::byte>                             non_contiguous{std::byte{0x01}};
    cbor::tags::detail::reader<std::deque<std::byte>> non_contiguous_reader{non_contiguous};
    CHECK_THROWS_AS(non_contiguous_reader.seek(-1), std::runtime_error);
    CHECK_THROWS_AS(non_contiguous_reader.seek(std::numeric_limits<std::ptrdiff_t>::min()), std::runtime_error);
}

TEST_CASE("decoder skips any byte strings on signed-size non-contiguous ranges") {
    SignedSizeDequeByteRange buffer{.bytes = {std::byte{0x41}, std::byte{0x01}, std::byte{0x02}}};

    auto         dec = make_decoder(buffer);
    as_bstr_any  decoded{};
    std::uint8_t following{};
    auto         result = dec(decoded, following);

    REQUIRE(result);
    CHECK_EQ(decoded.size, 1);
    CHECK_EQ(following, 2);
}

TEST_CASE("decoder skips any text strings on signed-size non-contiguous ranges") {
    SignedSizeDequeByteRange buffer{.bytes = {std::byte{0x61}, std::byte{0x41}, std::byte{0x02}}};

    auto         dec = make_decoder(buffer);
    as_text_any  decoded{};
    std::uint8_t following{};
    auto         result = dec(decoded, following);

    REQUIRE(result);
    CHECK_EQ(decoded.size, 1);
    CHECK_EQ(following, 2);
}

TEST_CASE("decoder should report incomplete byte-string view without retry contract") {
    // bstr(2): 0x42, but only 1 byte payload initially
    std::vector<std::byte> buffer{std::byte{0x42}, std::byte{0x01}};

    auto dec = make_decoder(buffer);

    std::basic_string_view<std::byte> decoded;
    auto                              result = dec(decoded);

    CHECK_FALSE_MESSAGE(result, "Truncated byte-string view should return incomplete.");
    CHECK_EQ(result.error(), status_code::incomplete);
}

TEST_CASE("decoder should report incomplete indefinite array without retry contract") {
    std::vector<std::byte> buffer{std::byte{0x9F}, std::byte{0x01}, std::byte{0x02}};

    auto dec = make_decoder(buffer);

    std::vector<int> decoded{99};
    auto             result = dec(decoded);

    CHECK_FALSE_MESSAGE(result, "Truncated indefinite array should return incomplete.");
    CHECK_EQ(result.error(), status_code::incomplete);
}

TEST_CASE("decoder should report incomplete indefinite bstr without retry contract") {
    std::vector<std::byte> buffer{std::byte{0x5F}, std::byte{0x41}, std::byte{0xAA}};

    auto dec = make_decoder(buffer);

    std::vector<std::byte> decoded{std::byte{0xCC}};
    auto                   result = dec(decoded);

    CHECK_FALSE_MESSAGE(result, "Truncated indefinite bstr should return incomplete.");
    CHECK_EQ(result.error(), status_code::incomplete);
}

TEST_CASE("decoder should decode complete definite values in one shot") {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(std::vector<int>{1, 2, 3}, std::map<int, int>{{1, 2}}, std::string{"ok"}));

    auto dec = make_decoder(buffer);

    std::vector<int>   values;
    std::map<int, int> mapping;
    std::string        label;
    auto               result = dec(values, mapping, label);

    REQUIRE_MESSAGE(result, "Complete definite values should decode through the one-shot path.");
    CHECK_EQ(values, std::vector<int>{1, 2, 3});
    CHECK_EQ(mapping, (std::map<int, int>{{1, 2}}));
    CHECK_EQ(label, "ok");
}

TEST_CASE("decoder should decode complete non-contiguous definite and indefinite values in one shot") {
    std::deque<std::byte> buffer{
        std::byte{0x82}, std::byte{0x01}, std::byte{0x02},                  // array [1, 2]
        std::byte{0xA1}, std::byte{0x01}, std::byte{0x02},                  // map {1: 2}
        std::byte{0x9F}, std::byte{0x03}, std::byte{0x04}, std::byte{0xFF}, // indefinite array [3, 4]
        std::byte{0xBF}, std::byte{0x03}, std::byte{0x04}, std::byte{0xFF}, // indefinite map {3: 4}
        std::byte{0x5F}, std::byte{0x41}, std::byte{0xAA}, std::byte{0xFF}, // indefinite bstr h'AA'
        std::byte{0x7F}, std::byte{0x61}, std::byte{'x'},  std::byte{0xFF}, // indefinite tstr "x"
    };

    auto dec = make_decoder(buffer);

    std::vector<int>       definite_values;
    std::map<int, int>     definite_mapping;
    std::vector<int>       indefinite_values;
    std::map<int, int>     indefinite_mapping;
    std::vector<std::byte> bytes;
    std::string            text;
    auto                   result = dec(definite_values, definite_mapping, indefinite_values, indefinite_mapping, bytes, text);

    REQUIRE_MESSAGE(result, "Complete non-contiguous definite and indefinite values should decode in one shot.");
    CHECK_EQ(definite_values, std::vector<int>{1, 2});
    CHECK_EQ(definite_mapping, (std::map<int, int>{{1, 2}}));
    CHECK_EQ(indefinite_values, std::vector<int>{3, 4});
    CHECK_EQ(indefinite_mapping, (std::map<int, int>{{3, 4}}));
    CHECK_EQ(bytes, std::vector<std::byte>{std::byte{0xAA}});
    CHECK_EQ(text, "x");
}

TEST_CASE("decoder should decode definite containers without requiring indefinite temporary targets") {
    using NonDefaultMap    = std::map<int, int, NonDefaultComparator>;
    using NonAssignableMap = std::map<int, int, NonAssignableComparator>;
    using NonDefaultVector = std::vector<int, NonDefaultAllocator<int>>;

    std::vector<std::byte> buffer{
        std::byte{0xA2}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}, // map {1: 2, 3: 4}
        std::byte{0xA1}, std::byte{0x05}, std::byte{0x06},                                   // map {5: 6}
        std::byte{0x82}, std::byte{0x07}, std::byte{0x08},                                   // array [7, 8]
    };

    auto dec = make_decoder(buffer);

    NonDefaultMap    non_default_map{NonDefaultComparator{1}};
    NonAssignableMap non_assignable_map{NonAssignableComparator{2}};
    NonDefaultVector non_default_vector{NonDefaultAllocator<int>{3}};
    auto             result = dec(non_default_map, non_assignable_map, non_default_vector);

    REQUIRE_MESSAGE(result, "Definite decode must not instantiate indefinite temporary requirements.");
    CHECK_EQ(non_default_map.size(), 2);
    CHECK_EQ(non_default_map[1], 2);
    CHECK_EQ(non_default_map[3], 4);
    CHECK_EQ(non_assignable_map.size(), 1);
    CHECK_EQ(non_assignable_map[5], 6);
    CHECK_EQ(non_default_vector.size(), 2);
    CHECK_EQ(non_default_vector[0], 7);
    CHECK_EQ(non_default_vector[1], 8);
    CHECK_EQ(non_default_vector.get_allocator().tag, 3);
}

TEST_CASE("decoder should decode indefinite containers without staging through assignable temporaries") {
    using NonDefaultMap    = std::map<int, int, NonDefaultComparator>;
    using NonAssignableMap = std::map<int, int, NonAssignableComparator>;
    using NonDefaultVector = std::vector<int, NonDefaultAllocator<int>>;

    std::vector<std::byte> buffer{
        std::byte{0xBF}, std::byte{0x01}, std::byte{0x02}, std::byte{0xFF}, // indefinite map {1: 2}
        std::byte{0xBF}, std::byte{0x03}, std::byte{0x04}, std::byte{0xFF}, // indefinite map {3: 4}
        std::byte{0x9F}, std::byte{0x05}, std::byte{0x06}, std::byte{0xFF}, // indefinite array [5, 6]
    };

    auto dec = make_decoder(buffer);

    NonDefaultMap    decoded_map{NonDefaultComparator{4}};
    NonAssignableMap decoded_non_assignable_map{NonAssignableComparator{5}};
    NonDefaultVector decoded_vector{NonDefaultAllocator<int>{5}};
    auto             result = dec(decoded_map, decoded_non_assignable_map, decoded_vector);

    REQUIRE_MESSAGE(result, "Indefinite decode should not require assigning a staged container back to the target.");
    CHECK_EQ(decoded_map.size(), 1);
    CHECK_EQ(decoded_map[1], 2);
    CHECK_EQ(decoded_map.key_comp().tag, 4);
    CHECK_EQ(decoded_non_assignable_map.size(), 1);
    CHECK_EQ(decoded_non_assignable_map[3], 4);
    CHECK_EQ(decoded_vector.size(), 2);
    CHECK_EQ(decoded_vector[0], 5);
    CHECK_EQ(decoded_vector[1], 6);
    CHECK_EQ(decoded_vector.get_allocator().tag, 5);
}

TEST_CASE("decoder should validate as_text_any length against available bytes") {
    // tstr(5): 0x65, but only 3 bytes payload -> truncated
    std::vector<std::byte> buffer{std::byte{0x65}, std::byte{0x61}, std::byte{0x62}, std::byte{0x63}};

    auto dec = make_decoder(buffer);

    as_text_any header{};
    auto        result = dec(header);

    CHECK_FALSE_MESSAGE(result, "Decoding as_text_any on truncated input should fail.");
    CHECK_EQ(result.error(), status_code::incomplete);
}

TEST_CASE("decoder should not walk past end for as_text_any on non-contiguous truncated input") {
    // tstr(5): 0x65, but only 1 byte payload; decoding another item after skipping would be UB.
    std::deque<std::byte> buffer{std::byte{0x65}, std::byte{'a'}};

    auto dec = make_decoder(buffer);

    as_text_any  header{};
    std::uint8_t next_value{};
    auto         result = dec(header, next_value);

    CHECK_FALSE_MESSAGE(result, "Truncated as_text_any must fail before attempting to advance non-contiguous iterators.");
    CHECK_EQ(result.error(), status_code::incomplete);
    CHECK_EQ(header.size, 5);
}

TEST_CASE("decoder should not advance non-contiguous iterators past end for as_bstr_any") {
    // bstr(1): 0x41 0xAA, then bstr(1): 0x41 but missing payload
    std::deque<std::uint8_t> buffer{0x41, 0xAA, 0x41};

    auto dec = make_decoder(buffer);

    as_bstr_any first{};
    as_bstr_any second{};
    auto        result = dec(first, second);

    CHECK_FALSE_MESSAGE(result, "Second as_bstr_any should detect incomplete payload without advancing past end.");
    CHECK_EQ(result.error(), status_code::incomplete);
    CHECK_EQ(first.size, 1);
    CHECK_EQ(second.size, 1);
}

TEST_CASE("decoder non-contiguous bstr_view should update offset for subsequent bounds checks") {
    // bstr(5): 0x45 01 02 03 04 05, then bstr(3): 0x43 AA (truncated payload)
    // Regression: if non-contiguous bstr decode doesn't keep current_offset_ in sync, the next header can skip past end (ASAN/crash).
    std::deque<std::byte> buffer{std::byte{0x45}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
                                 std::byte{0x04}, std::byte{0x05}, std::byte{0x43}, std::byte{0xAA}};

    auto dec = make_decoder(buffer);

    decltype(dec)::bstr_view_t first_view{};
    as_bstr_any                second_header{};
    std::uint8_t               next_value{};
    auto                       result = dec(first_view, second_header, next_value);

    CHECK_FALSE_MESSAGE(result, "Second header should fail as incomplete without advancing beyond end.");
    CHECK_EQ(result.error(), status_code::incomplete);
    CHECK_EQ(second_header.size, 3);
}

TEST_CASE("decoder should reject array length mismatch for fixed-size containers") {
    // array(3): 0x83, items: 1,2,3
    std::vector<std::byte> buffer{std::byte{0x83}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    auto dec = make_decoder(buffer);

    std::array<int, 2> decoded{};
    decoded.fill(-1);
    auto result = dec(decoded);

    CHECK_FALSE_MESSAGE(result, "Decoding into a fixed-size container should validate array length.");
    CHECK_EQ(result.error(), status_code::unexpected_group_size);
    CHECK_EQ(decoded[0], -1);
    CHECK_EQ(decoded[1], -1);
}

TEST_CASE("decoder should reject array length mismatch for fixed-size spans") {
    // array(3): 0x83, items: 1,2,3
    // Regression: previously this could write past the end of the span (ASAN heap-buffer-overflow).
    std::vector<std::byte> buffer{std::byte{0x83}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    auto dec = make_decoder(buffer);

    std::vector<int> storage(2, -1);
    std::span<int>   decoded{storage};
    auto             result = dec(decoded);

    CHECK_FALSE_MESSAGE(result, "Decoding into a fixed-size span should validate array length.");
    CHECK_EQ(result.error(), status_code::unexpected_group_size);
    CHECK_EQ(storage[0], -1);
    CHECK_EQ(storage[1], -1);
}

TEST_CASE("decoder duplicate map keys follow target container insertion semantics") {
    auto bytes = to_bytes("a201010102");

    {
        auto               dec = make_decoder(bytes);
        std::map<int, int> decoded;
        auto               result = dec(decoded);
        REQUIRE(result);
        CHECK_EQ(decoded.size(), 1);
        CHECK_EQ(decoded.at(1), 2);
    }

    {
        auto                    dec = make_decoder(bytes);
        std::multimap<int, int> decoded;
        auto                    result = dec(decoded);
        REQUIRE(result);
        CHECK_EQ(decoded.count(1), 2);
    }
}

TEST_CASE("encoder should not overflow fixed-size output buffers") {
    // Regression: previously, encoding would blindly write past the end of a fixed-size buffer (ASAN).
    std::array<std::byte, 0> buffer{};
    auto                     enc = make_encoder(buffer);

    auto result = enc(1u);

    CHECK_FALSE_MESSAGE(result, "Encoding into a zero-sized output buffer should fail safely.");
    CHECK_EQ(result.error(), status_code::error);
}

TEST_CASE("encoder should not overflow fixed-size output buffers for strings") {
    // Regression: previously, encoding a string payload into too-small output buffer could overflow (ASAN).
    std::array<std::byte, 1> buffer{};
    auto                     enc = make_encoder(buffer);

    auto result = enc("hello"sv);

    CHECK_FALSE_MESSAGE(result, "Encoding into a too-small fixed buffer should fail safely.");
    CHECK_EQ(result.error(), status_code::error);
}
