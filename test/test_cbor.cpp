#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_operators.h"
#include "cbor_tags/float16_ieee754.h"
#include "test_util.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <exception>
#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <format>
#include <functional>
#include <list>
#include <map>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

using namespace cbor::tags;

TEST_CASE("CBOR Encoder") {

    SUBCASE("Encode unsigned integers") {
        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);
        auto                   dec = make_decoder(data);

        enc(as_array{10}, 0, 1, 23, 24, 255, 256, 65535, 65536, 4294967295, 4294967296);

        REQUIRE_EQ(to_hex(data), "8a000117181818ff19010019ffff1a000100001affffffff1b0000000100000000");

        std::array<std::uint64_t, 10> values;
        static_assert(IsUnsigned<decltype(values)::value_type>);
        dec(values);
        CHECK_EQ(values[0], 0);
        CHECK_EQ(values[1], 1);
        CHECK_EQ(values[2], 23);
        CHECK_EQ(values[3], 24);
        CHECK_EQ(values[4], 255);
        CHECK_EQ(values[5], 256);
        CHECK_EQ(values[6], 65535);
        CHECK_EQ(values[7], 65536);
        CHECK_EQ(values[8], 4294967295);
        CHECK_EQ(values[9], 4294967296);

        int  first, second, third;
        auto dec2 = make_decoder(data);
        dec2(as_array{10}, first, second, third);
        CHECK_EQ(first, 0);
        CHECK_EQ(second, 1);
        CHECK_EQ(third, 23);
    }

    SUBCASE("Encode signed integers") {
        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);
        auto                   dec = make_decoder(data);

        enc(as_array{8}, 0, -1, -24, -25, -256, -257, -4294967296, -42949672960);

        REQUIRE_EQ(to_hex(data), "88002037381838ff3901003affffffff3b00000009ffffffff");

        std::array<std::int64_t, 8> values;
        static_assert(IsSigned<decltype(values)::value_type>);
        dec(values);
        CHECK_EQ(values[0], 0); // Will be unsigned
        CHECK_EQ(values[1], -1);
        CHECK_EQ(values[2], -24);
        CHECK_EQ(values[3], -25);
        CHECK_EQ(values[4], -256);
        CHECK_EQ(values[5], -257);
        CHECK_EQ(values[6], -4294967296);
        CHECK_EQ(values[7], -42949672960);
    }

    SUBCASE("Encode Text Strings") {
        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);
        auto                   dec = make_decoder(data);

        enc(as_array{3}, "IETF", "", "Hello world!");

        REQUIRE_EQ(to_hex(data), "836449455446606c48656c6c6f20776f726c6421");

        std::array<std::string, 3> values;
        CHECK(IsTextString<decltype(values)::value_type>);
        dec(values);

        CHECK_EQ(values[0], "IETF");
        CHECK_EQ(values[1], "");
        CHECK_EQ(values[2], "Hello world!");
    }

    SUBCASE("Encode Binary Strings") {
        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);
        auto                   dec = make_decoder(data);

        std::array<std::byte, 3> binary1{std::byte(0x01), std::byte(0x02), std::byte(0x03)};
        std::array<std::byte, 1> binary2{std::byte(0x00)};

        enc(as_array{2}, binary1, binary2);

        REQUIRE_EQ(to_hex(data), "82430102034100");

        std::array<std::vector<std::byte>, 2> values;
        CHECK(IsBinaryString<decltype(values)::value_type>);
        dec(values);

        CHECK(std::equal(values[0].begin(), values[0].end(), binary1.begin()));
        CHECK(std::equal(values[1].begin(), values[1].end(), binary2.begin()));
    }

    SUBCASE("Encode arrays") {
        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);
        auto                   dec = make_decoder(data);

        enc(as_array{4}, std::vector<int>{1, 2, 3}, std::array<int, 3>{4, 5, 6}, std::deque<int>{7, 8, 9}, std::list<int>{10, 11, 12});

        REQUIRE_EQ(to_hex(data), "84830102038304050683070809830a0b0c");

        std::array<std::vector<int>, 4> values;
        static_assert(IsRangeOfCborValues<decltype(values)::value_type>);
        dec(values);

        CHECK_EQ(values[0], std::vector<int>{1, 2, 3});
        CHECK_EQ(values[1], std::vector<int>{4, 5, 6});
        CHECK_EQ(values[2], std::vector<int>{7, 8, 9});
        CHECK_EQ(values[3], std::vector<int>{10, 11, 12});

        // Make big vector
        {
            std::vector<int> big_vector(1e5);
            std::iota(big_vector.begin(), big_vector.end(), -1e4);
            enc(big_vector);

            std::deque<int> big_vector_result;
            dec(big_vector_result);
            CHECK(std::equal(big_vector.begin(), big_vector.end(), big_vector_result.begin()));
        }
    }

    SUBCASE("Encode floats") {
        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);
        auto                   dec = make_decoder(data);

        enc(as_array{3}, float16_t(3.14f), float(3.14f), double(3.14));

        REQUIRE_EQ(to_hex(data), "83f94247fa4048f5c3fb40091eb851eb851f");

        std::array<std::variant<float16_t, float, double>, 3> values;
        static_assert(IsSimple<float16_t>);
        static_assert(!IsFloat16<float>);
        static_assert(IsFloat32<float>);

        static_assert(IsSimple<float>);
        static_assert(IsSimple<double>);

        dec(values);
        auto f16          = static_cast<float>(std::get<float16_t>(values[0]));
        auto expected_f16 = static_cast<float>(float16_t(3.14f));
        CHECK_EQ(f16, expected_f16);
        CHECK_EQ(std::get<float>(values[1]), 3.14f);
        CHECK_EQ(std::get<double>(values[2]), 3.14);
    }

    SUBCASE("Encode bools") {
        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);
        auto                   dec = make_decoder(data);

        enc(as_array{2}, true, false);

        REQUIRE_EQ(to_hex(data), "82f5f4");

        std::array<bool, 2> values;
        dec(values);
        CHECK_EQ(values[0], true);
        CHECK_EQ(values[1], false);
    }

    SUBCASE("Encode null") {
        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);
        auto                   dec = make_decoder(data);

        enc(as_array{2}, std::optional<int>(std::nullopt), 42);

        REQUIRE_EQ(to_hex(data), "82f6182a");

        std::array<std::optional<int>, 2> values;
        dec(values);
        CHECK_EQ(values[0], std::nullopt);
        CHECK_EQ(values[1].value(), 42);
    }

    SUBCASE("Encode maps") {
        {
            std::vector<std::byte> data;
            auto                   enc = make_encoder(data);
            auto                   dec = make_decoder(data);

            std::map<int, int> map{{1, 2}, {3, 4}, {5, 6}};
            enc(map);

            REQUIRE_EQ(to_hex(data), "a3010203040506");

            std::map<int, int> map_result;
            dec(map_result);

            CHECK_EQ(map_result, map);
        }

        {
            std::vector<std::byte> data;
            auto                   enc = make_encoder(data);
            auto                   dec = make_decoder(data);

            std::unordered_map<int, int> unordered_map{{1, 2}, {3, 4}, {5, 6}};
            enc(unordered_map);

            std::unordered_map<int, int> map_result;
            dec(map_result);

            CHECK_EQ(map_result, unordered_map);
        }

        // Unordered map of string_view
        {
            std::vector<std::byte> data;
            auto                   enc = make_encoder(data);
            auto                   dec = make_decoder(data);

            std::unordered_map<std::string_view, std::string_view> unordered_map{{"a", "b"}, {"c", "d"}, {"e", "f"}};
            enc(unordered_map);

            // Could look like "a3616561666163616461616162"

            std::unordered_map<std::string_view, std::string_view> map_result;
            dec(map_result);

            CHECK_EQ(map_result, unordered_map);
        }
    }

    SUBCASE("Encode map of floats") {
        std::map<float, float> float_map;
        float_map.insert({1.0f, 3.14159f});
        // a1fa3f800000fa40490fd0
        auto expected =
            std::vector<std::byte>{std::byte(0xA1), std::byte(0xFA), std::byte(0x3F), std::byte(0x80), std::byte(0x00), std::byte(0x00),
                                   std::byte(0xFA), std::byte(0x40), std::byte(0x49), std::byte(0x0F), std::byte(0xD0)};

        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);
        auto                   dec = make_decoder(data);
        enc(float_map);

        fmt::print("Float map: ");
        print_bytes(data);
        CHECK(data == expected);

        std::map<float, float> map_result;
        dec(map_result);
        CHECK_EQ(map_result, float_map);
    }

    SUBCASE("Encode nested structures") {
        struct A {
            float16_t a;
            double    b;
            bool      operator==(const A &rhs) const { return a.value == rhs.a.value && b == rhs.b; }
        };
        using tagged_A = std::pair<tag<511>, A>;

        using variant = std::variant<int, std::string, float, tagged_A>;

        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);
        auto                   dec = make_decoder(data);

        std::vector<variant> number_and_stuff = {1, 2, "hello", 3, 4.0f, make_tag_pair(tag<511>{}, A{3.14f, 3.14})};
        enc(number_and_stuff);
        fmt::print("Number and stuff: {}\n", to_hex(data));
        REQUIRE_EQ(to_hex(data), "8601026568656c6c6f03fa40800000d901fff94247fb40091eb851eb851f");

        std::vector<variant> number_and_stuff_result;
        dec(number_and_stuff_result);

        REQUIRE_EQ(number_and_stuff_result.size(), number_and_stuff.size());
        for (std::size_t i = 0; i < number_and_stuff.size(); ++i) {
            auto index_same = number_and_stuff[i].index() == number_and_stuff_result[i].index();
            CHECK(index_same);

            if (std::holds_alternative<int>(number_and_stuff[i])) {
                CHECK_EQ(std::get<int>(number_and_stuff[i]), std::get<int>(number_and_stuff_result[i]));
            } else if (std::holds_alternative<std::string>(number_and_stuff[i])) {
                CHECK_EQ(std::get<std::string>(number_and_stuff[i]), std::get<std::string>(number_and_stuff_result[i]));
            } else if (std::holds_alternative<float>(number_and_stuff[i])) {
                CHECK_EQ(std::get<float>(number_and_stuff[i]), std::get<float>(number_and_stuff_result[i]));
            } else if (std::holds_alternative<tagged_A>(number_and_stuff[i])) {
                CHECK_EQ(std::get<tagged_A>(number_and_stuff[i]).second, std::get<tagged_A>(number_and_stuff_result[i]).second);
            }
        }
    }
}

using variant = std::variant<int, std::string, float, double, bool, std::nullptr_t>;

template <typename Compare = std::less<>> struct VariantCompare {
    template <typename Variant> bool operator()(const Variant &lhs, const Variant &rhs) const {
        if (lhs.index() != rhs.index()) {
            return Compare{}(lhs.index(), rhs.index());
        }

        return std::visit(
            [](const auto &l, const auto &r) -> bool {
                using L = std::decay_t<decltype(l)>;
                using R = std::decay_t<decltype(r)>;

                if constexpr (std::is_same_v<L, std::nullptr_t> && std::is_same_v<R, std::nullptr_t>) {
                    return false; // nullptr == nullptr
                } else if constexpr (std::is_floating_point_v<L> && std::is_floating_point_v<R>) {
                    return Compare{}(l, r);
                } else if constexpr (std::is_same_v<L, R>) {
                    return Compare{}(l, r);
                } else {
                    return Compare{}(typeid(L).before(typeid(R)), false);
                }
            },
            lhs, rhs);
    }
};

TEST_CASE("Sorting strings and binary strings std::map") {
    std::map<variant, variant, variant_comparator<>> string_map = {{"c", true},  {"ac", false}, {"b", 3.0}, {"a", std::nullptr_t{}},
                                                                   {"ab", 5.0f}, {1, 3.0},      {-1, 3.0}};

    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);
    auto                   dec = make_decoder(data);

    enc(string_map);

    fmt::print("String map: {}\n", to_hex(data));

    CHECK_EQ(to_hex(data), "a720fb400800000000000001fb40080000000000006161f66162fb40080000000000006163f5626162fa40a00000626163f4");

    std::map<variant, variant, variant_comparator<>> map_result;
    dec(map_result);

    CHECK_EQ(map_result.size(), string_map.size());
    CHECK_EQ(map_result, string_map);
}

TEST_CASE("Test std::greater in std::map<variant,...>") {
    std::map<variant, variant, variant_comparator<>> string_map1 = {{"c", true},  {"ac", false}, {"b", 3.0}, {"a", std::nullptr_t{}},
                                                                    {"ab", 5.0f}, {1, 3.0},      {-1, 3.0}};

    std::map<variant, variant, variant_comparator<std::greater<>>> string_map2 = {
        {"c", true}, {"ac", false}, {"b", 3.0}, {"a", std::nullptr_t{}}, {"ab", 5.0f}, {1, 3.0}, {-1, 3.0}};

    std::vector<std::byte> data;
    std::string            hex1;
    std::string            hex2;

    {
        auto enc = make_encoder(data);
        enc(string_map1);
        hex1 = to_hex(data);
    }
    data.clear();
    {
        auto enc = make_encoder(data);
        enc(string_map2);
        hex2 = to_hex(data);
    }
    CHECK_EQ(string_map1.size(), string_map2.size());
    CHECK_NE(hex1, hex2);
    CHECK_EQ(hex1.size(), hex2.size());
}

TEST_CASE("Unordered maps") {
    std::unordered_map<variant, variant, variant_hasher> string_map = {{"c", true},  {"ac", false}, {"b", 3.0}, {"a", std::nullptr_t{}},
                                                                       {"ab", 5.0f}, {1, 3.0},      {-1, 3.0}};

    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);
    auto                   dec = make_decoder(data);

    enc(string_map);

    fmt::print("Unordered map: {}\n", to_hex(data));

    std::unordered_map<variant, variant, variant_hasher> map_result;
    dec(map_result);

    CHECK_EQ(map_result.size(), string_map.size());
    CHECK_EQ(map_result, string_map);
}

TEST_CASE("Sanity check equal size map unordered_map") {
    std::unordered_map<variant, variant, variant_hasher> string_map1 = {{"c", true},  {"ac", false}, {"b", 3.0}, {"a", std::nullptr_t{}},
                                                                        {"ab", 5.0f}, {1, 3.0},      {-1, 3.0}};
    std::map<variant, variant, variant_comparator<>>     string_map2 = {{"c", true},  {"ac", false}, {"b", 3.0}, {"a", std::nullptr_t{}},
                                                                        {"ab", 5.0f}, {1, 3.0},      {-1, 3.0}};
    CHECK_EQ(string_map1.size(), string_map2.size());
}

TEST_CASE("CBOR Encoder - Float encoding") {
    using namespace cbor::tags;
    // SUBCASE("Positive float") {
    //     float value   = 3.14159f;
    //     auto  encoded = encoder<>::serialize(value);
    //     fmt::print("Positive float: ");
    //     print_bytes(encoded);
    //     CHECK_EQ(to_hex(encoded), "fa40490fd0");
    // }

    // SUBCASE("Negative float") {
    //     float value   = -3.14159f;
    //     auto  encoded = encoder<>::serialize(value);
    //     fmt::print("Negative float: ");
    //     print_bytes(encoded);
    //     CHECK_EQ(to_hex(encoded), "fac0490fd0");
    // }

    // SUBCASE("Zero") {
    //     float value   = 0.0f;
    //     auto  encoded = encoder<>::serialize(value);
    //     fmt::print("Zero: ");
    //     print_bytes(encoded);
    //     CHECK_EQ(to_hex(encoded), "fa00000000");
    // }

    // SUBCASE("Infinity") {
    //     float value   = std::numeric_limits<float>::infinity();
    //     auto  encoded = encoder<>::serialize(value);
    //     fmt::print("Infinity: ");
    //     print_bytes(encoded);
    //     CHECK_EQ(to_hex(encoded), "fa7f800000");
    // }

    // SUBCASE("NaN") {
    //     float value   = std::numeric_limits<float>::quiet_NaN();
    //     auto  encoded = encoder<>::serialize(value);
    //     fmt::print("NaN: ");
    //     print_bytes(encoded);
    //     CHECK_EQ(to_hex(encoded), "fa7fc00000");
    // }

    // SUBCASE("Map of float sorted") {
    //     std::map<cbor::tags::variant_contiguous, cbor::tags::variant_contiguous> float_map;
    //     float_map = {{3.0f, 3.14159f},  {1.0f, 3.14159f},    {2.0f, 3.14159f}, {4.0f, 3.14159f}, {-5.0f, 3.14159f},
    //                  {-1.0f, 3.14159f}, {10.0f, 3.14159f},   {3.0, 3.14159f},  {-3.0, 3.14159f}, {"hello", 3.14159f},
    //                  {true, 3.14159f},  {nullptr, 3.14159f}, {1, 3.14159f},    {-2, 3.14159f},   {-3, 3.14159f}};

    //     auto encoded = cbor::tags::encoder<>::serialize(float_map);
    //     fmt::print("Float map sorted: ");
    //     print_bytes(encoded);

    //     /*
    //         {
    //             -3: 3.14159_2,
    //             -2: 3.14159_2,
    //             1: 3.14159_2,
    //             "hello": 3.14159_2,
    //             -5.0_2: 3.14159_2,
    //             -1.0_2: 3.14159_2,
    //             1.0_2: 3.14159_2,
    //             2.0_2: 3.14159_2,
    //             3.0_2: 3.14159_2,
    //             4.0_2: 3.14159_2,
    //             10.0_2: 3.14159_2,
    //             -3.0_3: 3.14159_2,
    //             3.0_3: 3.14159_2,
    //             true: 3.14159_2,
    //             null: 3.14159_2,
    //         }
    //     */
    //     CHECK_EQ(to_hex(encoded),
    //              "af22fa40490fd021fa40490fd001fa40490fd06568656c6c6ffa40490fd0fac0a00000fa40490fd0fabf800000fa40490fd0fa3f800000fa"
    //              "40490fd0fa40000000fa40490fd0fa40400000fa40490fd0fa40800000fa40490fd0fa41200000fa40490fd0fbc008000000000000fa4049"
    //              "0fd0fb4008000000000000fa40490fd0f5fa40490fd0f6fa40490fd0");
    // }
}

TEST_CASE("CBOR Encoder - Double encoding") {
    using namespace cbor::tags;
    SUBCASE("Positive double") {
        double value   = 3.14159265358979323846;
        auto   encoded = encoder<>::serialize(value);
        CHECK(encoded.size() == 9);
        CHECK(to_hex(encoded) == "fb400921fb54442d18");
    }

    SUBCASE("Negative double") {
        double value   = -3.14159265358979323846;
        auto   encoded = encoder<>::serialize(value);
        fmt::print("Negative double: ");
        print_bytes(encoded);
        CHECK(to_hex(encoded) == "fbc00921fb54442d18");
    }

    SUBCASE("Zero") {
        double value   = 0.0;
        auto   encoded = encoder<>::serialize(value);
        CHECK(to_hex(encoded) == "fb0000000000000000");
    }

    SUBCASE("Infinity") {
        double value   = std::numeric_limits<double>::infinity();
        auto   encoded = encoder<>::serialize(value);
        CHECK(to_hex(encoded) == "fb7ff0000000000000");
    }

    SUBCASE("NaN") {
        double value   = std::numeric_limits<double>::quiet_NaN();
        auto   encoded = encoder<>::serialize(value);
        CHECK(encoded.size() == 9);
        CHECK(encoded[0] == static_cast<std::byte>(0xFB));
        CHECK((encoded[1] & static_cast<std::byte>(0x7F)) == static_cast<std::byte>(0x7F));
        CHECK((encoded[2] & static_cast<std::byte>(0xF0)) == static_cast<std::byte>(0xF0));
        // The last 6 bytes can vary, so we don't check them
    }
}

// TEST_CASE("cbor::tags decoder") {
//     using namespace cbor::tags;
//     const char *data  = "a2676e756d626572734f8501026568656c6c6f03fa40800000656f746865724483f4f5f6";
//     auto        bytes = to_bytes(data);

//     SUBCASE("integers") {
//         auto encoded = encoder<>::serialize(std::uint64_t(4294967296));
//         auto decoded = decoder<>::deserialize(encoded);
//         // REQUIRE(decoded);
//         REQUIRE(std::holds_alternative<std::uint64_t>(decoded));
//         CHECK_EQ(std::get<std::uint64_t>(decoded), 4294967296);

//         encoded = encoder<>::serialize(std::int64_t(-100));
//         decoded = decoder<>::deserialize(encoded);
//         REQUIRE(std::holds_alternative<std::int64_t>(decoded));
//         CHECK_EQ(std::get<std::int64_t>(decoded), -100);

//         encoded = encoder<>::serialize(std::int64_t(-4294967296));
//         decoded = decoder<>::deserialize(encoded);
//         // REQUIRE(decoded);
//         REQUIRE(std::holds_alternative<std::int64_t>(decoded));
//         CHECK_EQ(std::get<std::int64_t>(decoded), -4294967296);

//         encoded = encoder<>::serialize(std::int64_t(-42949672960));
//         decoded = decoder<>::deserialize(encoded);
//         // REQUIRE(decoded);
//         REQUIRE(std::holds_alternative<std::int64_t>(decoded));
//         CHECK_EQ(std::get<std::int64_t>(decoded), -42949672960);

//         // Check small negative
//         encoded = encoder<>::serialize(std::int64_t(-24));
//         fmt::print("Small negative: {}\n", to_hex(encoded));
//         decoded = decoder<>::deserialize(encoded);
//         // fmt::print("Small negative decoded: {}\n", std::get<std::int64_t>(decoded));

//         // REQUIRE(decoded);
//         REQUIRE(std::holds_alternative<std::int64_t>(decoded));
//         CHECK_EQ(std::get<std::int64_t>(decoded), -24);
//     }

//     SUBCASE("Text strings") {
//         auto encoded = encoder<>::serialize(std::string_view("IETF"));
//         auto decoded = decoder<>::deserialize(encoded);
//         // REQUIRE(decoded);
//         REQUIRE(std::holds_alternative<std::string_view>(decoded));
//         CHECK_EQ(std::get<std::string_view>(decoded), "IETF");

//         encoded = encoder<>::serialize(std::string("IETF"));
//         decoded = decoder<>::deserialize(encoded);
//         // REQUIRE(decoded);
//         REQUIRE(std::holds_alternative<std::string_view>(decoded));
//         CHECK_EQ(std::get<std::string_view>(decoded), "IETF");
//     }

//     SUBCASE("Binary strings") {
//         auto encoded = encoder<>::serialize(std::span<const std::byte>(bytes));
//         auto decoded = decoder<>::deserialize(encoded);
//         // REQUIRE(decoded);
//         REQUIRE(std::holds_alternative<std::span<const std::byte>>(decoded));
//         // Loop and check each byte
//         auto decoded_bytes = std::get<std::span<const std::byte>>(decoded);
//         CHECK_EQ(decoded_bytes.size(), bytes.size());
//         for (size_t i = 0; i < bytes.size(); ++i) {
//             CHECK_EQ(decoded_bytes[i], bytes[i]);
//         }
//     }

//     SUBCASE("Arrays") {
//         auto encoded = encoder<>::serialize(std::vector<variant_contiguous>{1, 2, 3});
//         auto decoded = decoder<>::deserialize(encoded);
//         REQUIRE(std::holds_alternative<binary_array_view>(decoded));
//         auto array         = std::get<binary_array_view>(decoded);
//         auto decoded_array = decoder<>::deserialize(array);
//         CHECK_EQ(decoded_array.size(), 3);
//         CHECK_EQ(std::get<std::uint64_t>(decoded_array[0]), 1);
//         CHECK_EQ(std::get<std::uint64_t>(decoded_array[1]), 2);
//         CHECK_EQ(std::get<std::uint64_t>(decoded_array[2]), 3);
//     }

//     SUBCASE("Maps") {
//         auto encoded = encoder<>::serialize(std::map<variant_contiguous, variant_contiguous>{{"ca", 1}, {"ba", 2}, {"a", 3}});
//         auto decoded = decoder<>::deserialize(encoded);
//         REQUIRE(std::holds_alternative<binary_map_view>(decoded));

//         auto map = decoder<>::deserialize<std::map<variant_contiguous, variant_contiguous>>(std::get<binary_map_view>(decoded));
//         CHECK_EQ(map.size(), 3);
//         CHECK_EQ(std::get<std::uint64_t>(map["ca"]), 1);
//         CHECK_EQ(std::get<std::uint64_t>(map["ba"]), 2);
//         CHECK_EQ(std::get<std::uint64_t>(map["a"]), 3);
//     }

//     SUBCASE("Binary string map") {
//         auto decoded = decoder<>::deserialize(std::span(bytes));
//         REQUIRE(std::holds_alternative<binary_map_view>(decoded));

//         auto map = decoder<>::deserialize<std::map<variant_contiguous, variant_contiguous>>(std::get<binary_map_view>(decoded));
//         CHECK_EQ(map.size(), 2);
//         CHECK(map.contains("numbers"));
//         CHECK(map.contains("other"));
//     }

//     SUBCASE("Floats unordered map") {
//         auto umap = std::unordered_map<variant_contiguous, variant_contiguous>{{1.0f, 2.0f}, {3.0f, 4.0f}, {5.0, 6.0f}};

//         auto encoded = encoder<>::serialize(umap);
//         fmt::print("Float map: {}\n", to_hex(encoded));
//         auto decoded = decoder<>::deserialize(encoded);
//         REQUIRE(std::holds_alternative<binary_map_view>(decoded));

//         auto map = decoder<>::deserialize<std::unordered_map<variant_contiguous,
//         variant_contiguous>>(std::get<binary_map_view>(decoded));

//         CHECK_EQ(std::get<float>(map[1.0f]), 2.0f);
//         CHECK_EQ(std::get<float>(map[3.0f]), 4.0f);
//         CHECK_EQ(std::get<float>(map[5.0]), 6.0f);
//         CHECK_THROWS_AS(std::get<double>(map[5.0]), std::exception);
//     }

//     SUBCASE("Doubles map") {
//         auto encoded = encoder<>::serialize(std::map<variant_contiguous, variant_contiguous>{{1.0, 2.0}, {3.0, 4.0}, {5.0, 6.0}});
//         auto decoded = decoder<>::deserialize(encoded);
//         REQUIRE(std::holds_alternative<binary_map_view>(decoded));

//         auto map = decoder<>::deserialize<std::map<variant_contiguous, variant_contiguous>>(std::get<binary_map_view>(decoded));
//         CHECK_EQ(map.size(), 3);
//         CHECK_EQ(std::get<double>(map[1.0]), 2.0);
//         CHECK_EQ(std::get<double>(map[3.0]), 4.0);
//         CHECK_EQ(std::get<double>(map.at(5.0)), 6.0);
//         CHECK_THROWS_AS(std::get<float>(map.at(5.0f)), std::exception);
//     }

//     SUBCASE("Double unordered map") {
//         auto encoded =
//             encoder<>::serialize(std::unordered_map<variant_contiguous, variant_contiguous>{{1.0, 2.0}, {3.0, 4.0}, {5.0f, 6.0}});
//         auto decoded = decoder<>::deserialize(encoded);
//         REQUIRE(std::holds_alternative<binary_map_view>(decoded));

//         auto s = std::format("{}", "decoded");

//         auto map = decoder<>::deserialize<std::unordered_map<variant_contiguous,
//         variant_contiguous>>(std::get<binary_map_view>(decoded)); CHECK_EQ(map.size(), 3); CHECK_EQ(std::get<double>(map[1.0]), 2.0);
//         CHECK_EQ(std::get<double>(map[3.0]), 4.0);
//         CHECK_EQ(std::get<double>(map.at(5.0f)), 6.0);
//         CHECK_THROWS_AS(std::get<double>(map.at(5.0)), std::out_of_range);
//     }

//     SUBCASE("Decode Simple values") {
//         auto encoded = encoder<>::serialize(true);
//         auto decoded = decoder<>::deserialize(encoded);
//         REQUIRE(std::holds_alternative<bool>(decoded));
//         CHECK_EQ(std::get<bool>(decoded), true);

//         encoded = encoder<>::serialize(false);
//         decoded = decoder<>::deserialize(encoded);
//         REQUIRE(std::holds_alternative<bool>(decoded));
//         CHECK_EQ(std::get<bool>(decoded), false);

//         encoded = encoder<>::serialize(nullptr);
//         decoded = decoder<>::deserialize(encoded);
//         REQUIRE(std::holds_alternative<std::nullptr_t>(decoded));
//     }
// }