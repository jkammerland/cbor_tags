#include <array>
#include <cbor_tags/cbor_concepts.h>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_integer.h>
#include <compare>
#include <cstdint>
#include <doctest/doctest.h>
#include <limits>
#include <vector>

using namespace cbor::tags;

namespace {

template <typename T>
concept HasUnaryPlus = requires(T value) { +value; };
template <typename T>
concept HasUnaryMinus = requires(T value) { -value; };
template <typename L, typename R>
concept HasAddition = requires(L lhs, R rhs) { lhs + rhs; };
template <typename L, typename R>
concept HasSubtraction = requires(L lhs, R rhs) { lhs - rhs; };
template <typename L, typename R>
concept HasMultiplication = requires(L lhs, R rhs) { lhs * rhs; };
template <typename L, typename R>
concept HasDivision = requires(L lhs, R rhs) { lhs / rhs; };
template <typename L, typename R>
concept HasModulo = requires(L lhs, R rhs) { lhs % rhs; };
template <typename T>
concept HasAdditionAssignment = requires(T lhs, T rhs) { lhs += rhs; };
template <typename T>
concept HasSubtractionAssignment = requires(T lhs, T rhs) { lhs -= rhs; };
template <typename T>
concept HasMultiplicationAssignment = requires(T lhs, T rhs) { lhs *= rhs; };
template <typename T>
concept HasDivisionAssignment = requires(T lhs, T rhs) { lhs /= rhs; };
template <typename T>
concept HasModuloAssignment = requires(T lhs, T rhs) { lhs %= rhs; };
template <typename T>
concept HasPreIncrement = requires(T value) { ++value; };
template <typename T>
concept HasPostIncrement = requires(T value) { value++; };
template <typename T>
concept HasPreDecrement = requires(T value) { --value; };
template <typename T>
concept HasPostDecrement = requires(T value) { value--; };

template <typename T>
constexpr bool has_self_arithmetic =
    HasUnaryPlus<T> || HasUnaryMinus<T> || HasAddition<T, T> || HasSubtraction<T, T> || HasMultiplication<T, T> || HasDivision<T, T> ||
    HasModulo<T, T> || HasAdditionAssignment<T> || HasSubtractionAssignment<T> || HasMultiplicationAssignment<T> ||
    HasDivisionAssignment<T> || HasModuloAssignment<T> || HasPreIncrement<T> || HasPostIncrement<T> || HasPreDecrement<T> ||
    HasPostDecrement<T>;

template <typename L, typename R>
constexpr bool has_binary_arithmetic =
    HasAddition<L, R> || HasSubtraction<L, R> || HasMultiplication<L, R> || HasDivision<L, R> || HasModulo<L, R>;

static_assert(!has_self_arithmetic<negative>);
static_assert(!has_self_arithmetic<integer>);
static_assert(!has_binary_arithmetic<negative, positive>);
static_assert(!has_binary_arithmetic<positive, negative>);
static_assert(!has_binary_arithmetic<negative, integer>);
static_assert(!has_binary_arithmetic<integer, negative>);
static_assert(!has_binary_arithmetic<integer, positive>);
static_assert(!has_binary_arithmetic<positive, integer>);
static_assert(std::three_way_comparable<negative>);
static_assert(std::three_way_comparable<integer>);

} // namespace

TEST_CASE("Test IsNegative concept") {
    static_assert(IsNegative<negative>);
    static_assert(!IsNegative<int>);
}

TEST_CASE("Basic conversion") {
    negative n(10);
    CHECK_EQ(n.value, 10);

    negative n2(std::numeric_limits<std::uint64_t>::max());
    CHECK_EQ(n2.value, std::numeric_limits<std::uint64_t>::max());
    integer i = n2;

    CHECK_EQ(i.value, std::numeric_limits<std::uint64_t>::max());
    CHECK(i.is_negative);
}

TEST_CASE("negative literal preserves its wire magnitude") {
    constexpr auto value = 42_neg;
    static_assert(value == negative{42});
}

TEST_CASE("wire integer wrappers have numeric ordering across the full CBOR domain") {
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();

    CHECK(negative{0} < negative{max});
    CHECK(negative{max} < negative{2});
    CHECK(negative{2} < negative{1});

    CHECK(integer{negative{0}} < integer{negative{max}});
    CHECK(integer{negative{max}} < integer{negative{2}});
    CHECK(integer{negative{2}} < integer{negative{1}});
    CHECK(integer{negative{1}} < integer{positive{0}});
    CHECK(integer{positive{0}} < integer{max});
}

TEST_CASE("negative wrapper roundtrips the full CBOR negative domain") {
    constexpr auto max    = std::numeric_limits<std::uint64_t>::max();
    const auto     values = std::array{negative{1}, negative{2}, negative{max}, negative{0}};

    for (const auto original : values) {
        CAPTURE(original.value);
        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);
        REQUIRE(enc(original));

        negative decoded{1};
        auto     dec = make_decoder(data);
        REQUIRE(dec(decoded));
        CHECK_EQ(decoded, original);
    }
}

TEST_CASE("signed wire wrapper roundtrips positive and negative CBOR boundaries") {
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    const auto values  = std::array{integer{positive{0}}, integer{max}, integer{negative{1}}, integer{negative{max}}, integer{negative{0}}};

    for (const auto original : values) {
        CAPTURE(original.value);
        CAPTURE(original.is_negative);
        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);
        REQUIRE(enc(original));

        integer decoded{positive{0}};
        auto    dec = make_decoder(data);
        REQUIRE(dec(decoded));
        CHECK_EQ(decoded, original);
    }
}

TEST_CASE("Encode, Decode 0s") {
    auto data = std::vector<std::uint8_t>{};
    auto enc  = make_encoder(data);
    REQUIRE(enc(0));
    REQUIRE(enc(-1));

    auto dec = make_decoder(data);
    int  result;
    REQUIRE(dec(result));
    CHECK_EQ(result, 0);

    REQUIRE(dec(result));
    CHECK_EQ(result, -1);
}
