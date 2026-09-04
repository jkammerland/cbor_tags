#include "test_util.h"

#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_reflection.h>
#include <span>
#include <tuple>

using namespace cbor::tags;

namespace {

struct leading_array {
    std::array<int, 3> values;
    int                id;
    bool               operator==(const leading_array &) const = default;
};

struct trailing_array {
    int                id;
    std::array<int, 3> values;
    bool               operator==(const trailing_array &) const = default;
};

struct only_array {
    std::array<int, 3> values;
    bool               operator==(const only_array &) const = default;
};

struct empty_array {
    std::array<int, 0> values;
    int                id;
    bool               operator==(const empty_array &) const = default;
};

struct matrix_member {
    std::array<std::array<int, 2>, 2> values;
    int                               id;
    bool                              operator==(const matrix_member &) const = default;
};

struct array_and_reference {
    std::array<int, 3> values;
    int               &id;
};

// Keep a C array's storage layout while bypassing automatic reflection.
struct raw_array_record {
    int values[3];
    int id;

    template <typename Encoder> auto encode(Encoder &enc) const { return enc(wrap_as_array{std::span{values}, id}); }
    template <typename Decoder> auto decode(Decoder &dec) {
        auto elements = std::span{values};
        return dec(wrap_as_array{elements, id});
    }
};

template <typename T> void check_roundtrip(const T &expected, std::string_view hex) {
    std::vector<std::byte> bytes;
    REQUIRE(make_encoder(bytes)(expected));
    CHECK_EQ(to_hex(bytes), hex);
    T decoded{};
    REQUIRE(make_decoder(bytes)(decoded));
    CHECK(decoded == expected);
}

} // namespace

TEST_CASE("std array members preserve reflection member counts") {
    CHECK_EQ(detail::aggregate_binding_count<leading_array>, 2);
    CHECK_EQ(detail::aggregate_binding_count<trailing_array>, 2);
    CHECK_EQ(detail::aggregate_binding_count<only_array>, 1);
    CHECK_EQ(detail::aggregate_binding_count<empty_array>, 2);
    CHECK_EQ(detail::aggregate_binding_count<matrix_member>, 2);
    CHECK_EQ(detail::aggregate_binding_count<array_and_reference>, 2);

    leading_array value{{1, 2, 3}, 4};
    auto          members   = to_tuple(value);
    std::get<0>(members)[1] = 9;
    CHECK_EQ(value.values[1], 9);
}

TEST_CASE("std array reflection workarounds preserve array wire shapes") {
    check_roundtrip(leading_array{{1, 2, 3}, 4}, "828301020304");
    check_roundtrip(trailing_array{4, {1, 2, 3}}, "820483010203");
    check_roundtrip(only_array{{1, 2, 3}}, "83010203");
    check_roundtrip(matrix_member{{{{1, 2}, {3, 4}}}, 5}, "828282010282030405");
}

TEST_CASE("std array members coexist with mutable references") {
    auto                bytes = to_bytes("828301020304");
    int                 id{};
    array_and_reference decoded{{}, id};
    REQUIRE(make_decoder(bytes)(decoded));
    CHECK_EQ(decoded.values, (std::array{1, 2, 3}));
    CHECK_EQ(id, 4);
}

TEST_CASE("custom codecs bypass reflection for raw array members") {
    const raw_array_record expected{{1, 2, 3}, 4};
    std::vector<std::byte> bytes;
    REQUIRE(make_encoder(bytes)(expected));
    CHECK_EQ(to_hex(bytes), "828301020304");
    raw_array_record decoded{};
    REQUIRE(make_decoder(bytes)(decoded));
    CHECK(std::ranges::equal(decoded.values, expected.values));
    CHECK_EQ(decoded.id, expected.id);

    auto       truncated = to_bytes("82830102");
    const auto result    = make_decoder(truncated)(decoded);
    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::incomplete);
}
