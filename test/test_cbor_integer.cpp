#include <cbor_tags/cbor_concepts.h>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_integer.h>
#include <doctest/doctest.h>
#include <limits>

using namespace cbor::tags;

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

TEST_CASE("Addition") {
    negative n(10);
    integer  i(20);

    auto result = n + i;
    CHECK_EQ(result.value, 20 - 10);
    CHECK(!result.is_negative);

    result = i + n;
    CHECK_EQ(result.value, 20 - 10);
    CHECK(!result.is_negative);

    result = n + n;
    CHECK_EQ(result.value, 10 + 10);
    CHECK(result.is_negative);

    result = n + std::uint64_t(9);
    CHECK_EQ(result.value, 1);
    CHECK(result.is_negative);

    result = std::uint64_t(9) + n;
    CHECK_EQ(result.value, 1);
    CHECK(result.is_negative);

    result = n + std::uint64_t(10);
    CHECK_EQ(result.value, 0);
    CHECK(!result.is_negative);

    {
        auto test = std::uint64_t(10) + std::numeric_limits<std::uint64_t>::max();
        REQUIRE_EQ(test + 1, 10); // Sanity check, + 1 should because of overflow
    }

    // Overflow negative
    result = n + negative(std::numeric_limits<std::uint64_t>::max());
    CHECK_EQ(result.value + 1, n.value); // Same as sanity check

    // Underflow negative
    result = n + std::uint64_t(11);
    CHECK_EQ(result.value, 1);
    CHECK(!result.is_negative);
}

TEST_CASE("Subtraction") {
    negative n(10);
    integer  i(20);

    auto result = n - i;
    CHECK_EQ(result.value, 30);
    CHECK(result.is_negative);

    result = i - n;
    CHECK_EQ(result.value, 30);
    CHECK(!result.is_negative);

    result = n - n;
    CHECK_EQ(result.value, 0);
    CHECK(!result.is_negative);

    result = n - std::uint64_t(9);
    CHECK_EQ(result.value, 19);
    CHECK(result.is_negative);

    result = std::uint64_t(9) - n;
    CHECK_EQ(result.value, 19);
    CHECK(!result.is_negative);

    result = n - std::uint64_t(10);
    CHECK_EQ(result.value, 20);
    CHECK(result.is_negative);

    // Overflow negative
    result = negative(std::numeric_limits<std::uint64_t>::max()) - std::uint64_t(n.value);
    CHECK_EQ(result.value + 1, n.value); // + 1 because of overflow
    CHECK(result.is_negative);

    // Underflow negative
    result = n + std::uint64_t(11);
    CHECK_EQ(result.value, 1);
    CHECK(!result.is_negative);
}

TEST_CASE("test max positive + min negative") {
    positive a = std::numeric_limits<std::uint64_t>::max();
    auto     b = negative(std::numeric_limits<std::uint64_t>::max());

    auto result = a + b;
    CHECK_EQ(result.value, 0);

    // Sanity check exactly zero
    result = 3ULL + 3_neg;
    CHECK_EQ(result.value, 0);

    result = -3ULL - 3_neg;
    CHECK_EQ(result.value, 0);

    result = 3_neg + 3ULL;
    CHECK_EQ(result.value, 0);
}

TEST_CASE("Just integer maths") {
    integer a(10);
    integer b(negative(20));

    auto result = a + b;
    CHECK_EQ(result.value, 10);
    CHECK(result.is_negative);

    CHECK_EQ(result, b + a);

    result = a - b;
    CHECK_EQ(result.value, 30);
    CHECK(!result.is_negative);

    CHECK_EQ(-result, b - a);

    // Overflow
    result = std::numeric_limits<std::uint64_t>::max() + 1;
    CHECK_EQ(result.value, 0);

    // Underflow
    result = -std::numeric_limits<std::uint64_t>::max() - 1;
    CHECK_EQ(result.value, 0);

    // Multiplication
    result = a * b;
    CHECK_EQ(result.value, 200);
    CHECK(result.is_negative);
    CHECK_EQ(result, b * a);

    result = b * 0;
    CHECK_EQ(result.value, 0);
    CHECK(!result.is_negative);

    result = a * b * a * b;
    CHECK_EQ(result.value, 40000);
    CHECK(!result.is_negative);

    // Division
    result = a / b;
    CHECK_EQ(result.value, 0);
    CHECK(!result.is_negative);

    result = b / a;
    CHECK_EQ(result.value, 2);
    CHECK(result.is_negative);

    result = a * b / b;
    CHECK_EQ(result.value, 10);
    CHECK(!result.is_negative);

    // Modulus
    result = a % b;
    CHECK_EQ(result.value, 10);
    CHECK(!result.is_negative);

    result = b % a;
    CHECK_EQ(result.value, 0);
    CHECK(!result.is_negative);
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
