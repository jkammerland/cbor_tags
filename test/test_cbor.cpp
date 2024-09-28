#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <span>
#include <stdexcept>
#include <string_view>
#include <variant>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <map>
#include <string>
#include <vector>

template <typename T> std::string to_hex(const T &bytes) {
    std::string hex;
    hex.reserve(bytes.size() * 2);

    fmt::format_to(std::back_inserter(hex), "{:02x}", fmt::join(bytes, ""));

    return hex;
}

std::vector<std::byte> to_bytes(std::string_view hex) {
    if (hex.length() % 2 != 0) {
        return {};
    }

    std::vector<std::byte> bytes;
    bytes.reserve(hex.length() / 2);

    auto byte_to_int = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        throw std::invalid_argument("Invalid hex character");
    };

    for (size_t i = 0; i < hex.length(); i += 2) {
        int high = byte_to_int(hex[i]);
        int low  = byte_to_int(hex[i + 1]);
        bytes.push_back(static_cast<std::byte>((high << 4) | low));
    }

    return bytes;
}

// Print using fmt
auto print_bytes = [](const std::vector<std::byte> &bytes) { fmt::print("{}\n", to_hex(bytes)); };

TEST_CASE("CBOR Encoder") {
    using namespace cbor;

    SUBCASE("Encode unsigned integers") {
        CHECK_EQ(Encoder::serialize(std::uint64_t(0)), std::vector<std::byte>{std::byte(0x00)});

        CHECK_EQ(Encoder::serialize(std::uint64_t(23)), std::vector<std::byte>{std::byte(0x17)});
        CHECK_EQ(Encoder::serialize(std::uint64_t(24)), std::vector<std::byte>{std::byte(0x18), std::byte(0x18)});
        CHECK_EQ(Encoder::serialize(std::uint64_t(255)), std::vector<std::byte>{std::byte(0x18), std::byte(0xFF)});
        CHECK_EQ(Encoder::serialize(std::uint64_t(256)), std::vector<std::byte>{std::byte(0x19), std::byte(0x01), std::byte(0x00)});
        CHECK_EQ(Encoder::serialize(std::uint64_t(65535)), std::vector<std::byte>{std::byte(0x19), std::byte(0xFF), std::byte(0xFF)});
        CHECK_EQ(Encoder::serialize(std::uint64_t(65536)),
                 std::vector<std::byte>{std::byte(0x1A), std::byte(0x00), std::byte(0x01), std::byte(0x00), std::byte(0x00)});
        CHECK_EQ(Encoder::serialize(std::uint64_t(4294967295)),
                 std::vector<std::byte>{std::byte(0x1A), std::byte(0xFF), std::byte(0xFF), std::byte(0xFF), std::byte(0xFF)});
        CHECK_EQ(Encoder::serialize(std::uint64_t(4294967296)),
                 std::vector<std::byte>{std::byte(0x1B), std::byte(0x00), std::byte(0x00), std::byte(0x00), std::byte(0x01),
                                        std::byte(0x00), std::byte(0x00), std::byte(0x00), std::byte(0x00)});
    }

    SUBCASE("Encode signed integers") {
        CHECK_EQ(Encoder::serialize(std::int64_t(0)), std::vector<std::byte>{std::byte(0x00)});
        CHECK_EQ(Encoder::serialize(std::int64_t(-1)), std::vector<std::byte>{std::byte(0x20)});
        CHECK_EQ(Encoder::serialize(std::int64_t(-24)), std::vector<std::byte>{std::byte(0x37)});
        CHECK_EQ(Encoder::serialize(std::int64_t(-25)), std::vector<std::byte>{std::byte(0x38), std::byte(0x18)});
        CHECK_EQ(Encoder::serialize(std::int64_t(-256)), std::vector<std::byte>{std::byte(0x38), std::byte(0xFF)});
        CHECK_EQ(Encoder::serialize(std::int64_t(-257)), std::vector<std::byte>{std::byte(0x39), std::byte(0x01), std::byte(0x00)});
        // Check big negative
        auto data = Encoder::serialize(std::int64_t(-4294967296));
        fmt::print("Big negative: {}\n", to_hex(data));
        CHECK_EQ(to_hex(data), "3affffffff");

        data = Encoder::serialize(std::int64_t(-42949672960));
        fmt::print("Biggest negative: {}\n", to_hex(data));
        CHECK_EQ(to_hex(data), "3b00000009ffffffff");
    }

    SUBCASE("Encode strings") {
        std::vector<std::byte> empty_string = Encoder::serialize(std::string_view(""));
        fmt::print("Empty string: ");
        print_bytes(empty_string);
        CHECK(empty_string == std::vector<std::byte>{std::byte(0x60)});

        std::vector<std::byte> ietf_string_view = Encoder::serialize(std::string_view("IETF"));
        fmt::print("IETF string view: ");
        print_bytes(ietf_string_view);
        CHECK(ietf_string_view ==
              std::vector<std::byte>{std::byte(0x64), std::byte(0x49), std::byte(0x45), std::byte(0x54), std::byte(0x46)});

        std::vector<std::byte> ietf_string = Encoder::serialize(std::string("IETF"));
        fmt::print("IETF string: ");
        print_bytes(ietf_string);
        CHECK(ietf_string == std::vector<std::byte>{std::byte(0x64), std::byte(0x49), std::byte(0x45), std::byte(0x54), std::byte(0x46)});
        std::vector<std::byte> ietf_c_string = Encoder::serialize("IETF");
        fmt::print("IETF C-string: ");
        print_bytes(ietf_c_string);
        CHECK(ietf_c_string == std::vector<std::byte>{std::byte(0x64), std::byte(0x49), std::byte(0x45), std::byte(0x54), std::byte(0x46)});
    }

    SUBCASE("Encode arrays") {
        std::vector<Value> empty_array;
        CHECK(Encoder::serialize(empty_array) == std::vector<std::byte>{std::byte(0x80)});

        std::vector<Value> array{1, 2, 3};
        CHECK(Encoder::serialize(array) == std::vector<std::byte>{std::byte(0x83), std::byte(0x01), std::byte(0x02), std::byte(0x03)});

        std::array<Value, 3> fixed_array{1, 2, 3};
        CHECK(Encoder::serialize(fixed_array) ==
              std::vector<std::byte>{std::byte(0x83), std::byte(0x01), std::byte(0x02), std::byte(0x03)});

        // Make big vector
        std::vector<Value> big_array;
        big_array.reserve(1000); // Reserve space for efficiency, but don't initialize
        for (std::uint64_t i = 0; i < 1000; ++i) {
            big_array.emplace_back(i);
        }
        auto big_array_encoded = Encoder::serialize(big_array);

        // You might also want to check the encoded size
        CHECK(big_array_encoded.size() > 1000);         // The encoded size should be larger than 1000 bytes
        CHECK(big_array_encoded[0] == std::byte{0x99}); // 0x9a is the CBOR header for an array of 1000 elements
        CHECK(big_array_encoded[1] == std::byte{0x03}); // First byte of 1000 (0x03E8)
        CHECK(big_array_encoded[2] == std::byte{0xE8}); // Second byte of 1000 (0x03E8)

        // 1903e7 is 999, so the last element should be 999
        CHECK(big_array_encoded[big_array_encoded.size() - 2] == std::byte{0x03});
        CHECK(big_array_encoded.back() == std::byte{0xE7});

        // fmt::print("Big array: ");
        // print_bytes(big_array_encoded);
    }

    SUBCASE("Encode maps") {
        {
            std::map<Value, Value> empty_map;
            CHECK(Encoder::serialize(empty_map) == std::vector<std::byte>{std::byte(0xA0)});

            std::map<Value, Value> map;
            map.insert({Value(1), Value(2)});
            // Should be 0xA1 0x01 0x02
            auto expected = std::vector<std::byte>{std::byte(0xA1), std::byte(0x01), std::byte(0x02)};
            auto encoded  = Encoder::serialize(map);
            fmt::print("Map: ");
            print_bytes(encoded);
            CHECK(encoded == expected);
        }

        {
            std::unordered_map<Value, Value> unordered_map{{1, 2}, {3, 4}, {5, 6}};
            // Note: The order of elements in an unordered_map is not guaranteed,
            // so we can't check for an exact byte sequence. Instead, we check the size.
            auto encoded = Encoder::serialize(unordered_map);
            CHECK(encoded.size() == 7);

            // Check that is contains the keys
            CHECK(unordered_map.find(1) != unordered_map.end());
            CHECK(unordered_map.find(3) != unordered_map.end());
            CHECK(unordered_map.find(5) != unordered_map.end());

            // Negative
            CHECK(unordered_map.find(2) == unordered_map.end());
            CHECK(unordered_map.find(4) == unordered_map.end());
            CHECK(unordered_map.find(6) == unordered_map.end());

            // Check that the values are correct
            CHECK(unordered_map[1] == 2);
            CHECK(unordered_map[3] == 4);
            CHECK(unordered_map[5] == 6);

            // Check encoded
            fmt::print("Unordered map: ");
            print_bytes(encoded);

            // Find the bytes for the keys
            auto key1 = std::find(encoded.begin(), encoded.end(), std::byte(0x01));
            CHECK(key1 != encoded.end());
            auto key3 = std::find(encoded.begin(), encoded.end(), std::byte(0x03));
            CHECK(key3 != encoded.end());
            auto key5 = std::find(encoded.begin(), encoded.end(), std::byte(0x05));
            CHECK(key5 != encoded.end());

            // Find the bytes for the values
            auto value2 = std::find(encoded.begin(), encoded.end(), std::byte(0x02));
            CHECK(value2 != encoded.end());
            auto value4 = std::find(encoded.begin(), encoded.end(), std::byte(0x04));
            CHECK(value4 != encoded.end());
            auto value6 = std::find(encoded.begin(), encoded.end(), std::byte(0x06));
            CHECK(value6 != encoded.end());
        }

        // Unordered map of string_view
        {
            std::unordered_map<Value, Value> string_map{{"a", "b"}, {"c", "d"}, {"e", "f"}};
            auto                             encoded = Encoder::serialize(string_map);
            fmt::print("String map: ");
            print_bytes(encoded);
            CHECK(encoded.size() == 13);

            // Check that is contains the keys
            CHECK(string_map.find("a") != string_map.end());
            CHECK(string_map.find("c") != string_map.end());
            CHECK(string_map.find("e") != string_map.end());
            // Negative
            CHECK(string_map.find("b") == string_map.end());
            CHECK(string_map.find("d") == string_map.end());
            // Check that the values are correct
            CHECK(string_map["a"] == "b");
            CHECK(string_map["c"] == "d");
            CHECK(string_map["e"] == "f");

            // Find the bytes for the keys
            auto key1 = std::find(encoded.begin(), encoded.end(), std::byte(0x61));
            CHECK(key1 != encoded.end());
            auto key3 = std::find(encoded.begin(), encoded.end(), std::byte(0x63));
            CHECK(key3 != encoded.end());
            auto key5 = std::find(encoded.begin(), encoded.end(), std::byte(0x65));
            CHECK(key5 != encoded.end());

            // Find the bytes for the values
            auto value2 = std::find(encoded.begin(), encoded.end(), std::byte(0x62));
            CHECK(value2 != encoded.end());
            auto value4 = std::find(encoded.begin(), encoded.end(), std::byte(0x64));
            CHECK(value4 != encoded.end());
            auto value6 = std::find(encoded.begin(), encoded.end(), std::byte(0x66));
            CHECK(value6 != encoded.end());
        }
    }

    SUBCASE("Encode map of floats") {
        std::map<Value, Value> float_map;
        float_map.insert({Value(1.0f), Value(3.14159f)});
        // a1fa3f800000fa40490fd0
        auto expected =
            std::vector<std::byte>{std::byte(0xA1), std::byte(0xFA), std::byte(0x3F), std::byte(0x80), std::byte(0x00), std::byte(0x00),
                                   std::byte(0xFA), std::byte(0x40), std::byte(0x49), std::byte(0x0F), std::byte(0xD0)};
        auto encoded = Encoder::serialize(float_map);
        fmt::print("Float map: ");
        print_bytes(encoded);
        CHECK(encoded == expected);
    }

    SUBCASE("Encode bool and null") {
        CHECK(Encoder::serialize(true) == std::vector<std::byte>{std::byte(0xF5)});
        CHECK(Encoder::serialize(false) == std::vector<std::byte>{std::byte(0xF4)});
        CHECK(Encoder::serialize(nullptr) == std::vector<std::byte>{std::byte(0xF6)});
    }

    SUBCASE("Encode nested structures") {
        std::vector<Value> number_and_stuff{1, 2, "hello", 3, 4.0f};
        auto               encoded1 = Encoder::serialize(number_and_stuff);
        fmt::print("encoded1: ");
        print_bytes(encoded1);
        CHECK_EQ(to_hex(encoded1), "8501026568656c6c6f03fa40800000");

        std::vector<Value> other_stuff{false, true, nullptr};
        auto               encoded2 = Encoder::serialize(other_stuff);
        fmt::print("encoded2: ");
        print_bytes(encoded2);
        CHECK_EQ(to_hex(encoded2), "83f4f5f6");

        std::map<Value, Value> map_of_arrays{{"numbers", std::span(encoded1)}, {"other", std::span(encoded2)}};
        auto                   encoded3 = Encoder::serialize(map_of_arrays);
        fmt::print("Nested map: ");
        print_bytes(encoded3);
        CHECK_EQ(to_hex(encoded3), "a2676e756d626572734f8501026568656c6c6f03fa40800000656f746865724483f4f5f6");
    }
}

TEST_CASE("Sorting strings and binary strings std::map") {
    {
        std::map<cbor::Value, cbor::Value> string_map;
        string_map.insert({"c", 1});
        string_map.insert({"ac", 2});
        string_map.insert({"b", 3});
        string_map.insert({"a", 4});
        string_map.insert({"ab", 5});

        auto encoded = cbor::Encoder::serialize(string_map);
        fmt::print("String map: {}\n", to_hex(encoded));
        CHECK_EQ(to_hex(encoded), "a56161046261620562616302616203616301");
    }

    // Binary strings
    {
        std::map<cbor::Value, cbor::Value> string_map;
        auto vec1 = std::vector<std::byte>({std::byte(0x05), std::byte(0x02), std::byte(0x03), std::byte(0x04), std::byte(0x05)});
        string_map.insert({std::span<std::byte>(vec1), 6});
        auto vec2 = std::vector<std::byte>({std::byte(0x01), std::byte(0x02), std::byte(0x03), std::byte(0x04)});
        string_map.insert({std::span<std::byte>(vec2), 7});
        auto vec3 = std::vector<std::byte>({std::byte(0x05), std::byte(0x02), std::byte(0x01), std::byte(0x04)});
        string_map.insert({std::span<std::byte>(vec3), 8});

        auto encoded = cbor::Encoder::serialize(string_map);
        fmt::print("Binary string map: {}\n", to_hex(encoded));
        CHECK_EQ(to_hex(encoded), "a344010203040744050201040845050203040506");
    }
}

TEST_CASE("CBOR Encoder - Float encoding") {
    cbor::Encoder encoder;

    SUBCASE("Positive float") {
        float value   = 3.14159f;
        auto  encoded = encoder.serialize(value);
        fmt::print("Positive float: ");
        print_bytes(encoded);
        CHECK(encoded.size() == 5);
        CHECK(encoded[0] == static_cast<std::byte>(0xFA));
        CHECK(encoded[1] == static_cast<std::byte>(0x40));
        CHECK(encoded[2] == static_cast<std::byte>(0x49));
        CHECK(encoded[3] == static_cast<std::byte>(0x0F));
        CHECK(encoded[4] == static_cast<std::byte>(0xD0));
    }

    SUBCASE("Negative float") {
        float value   = -3.14159f;
        auto  encoded = encoder.serialize(value);
        CHECK(encoded.size() == 5);
        fmt::print("Negative float: ");
        print_bytes(encoded);
        CHECK(encoded[0] == static_cast<std::byte>(0xFA));
        CHECK(encoded[1] == static_cast<std::byte>(0xC0));
        CHECK(encoded[2] == static_cast<std::byte>(0x49));
        CHECK(encoded[3] == static_cast<std::byte>(0x0F));
        CHECK(encoded[4] == static_cast<std::byte>(0xD0));
    }

    SUBCASE("Zero") {
        float value   = 0.0f;
        auto  encoded = encoder.serialize(value);
        CHECK(encoded.size() == 5);
        CHECK(encoded[0] == static_cast<std::byte>(0xFA));
        CHECK(encoded[1] == static_cast<std::byte>(0x00));
        CHECK(encoded[2] == static_cast<std::byte>(0x00));
        CHECK(encoded[3] == static_cast<std::byte>(0x00));
        CHECK(encoded[4] == static_cast<std::byte>(0x00));
    }

    SUBCASE("Infinity") {
        float value   = std::numeric_limits<float>::infinity();
        auto  encoded = encoder.serialize(value);
        fmt::print("Infinity: ");
        print_bytes(encoded);
        CHECK(encoded.size() == 5);
        CHECK(encoded[0] == static_cast<std::byte>(0xFA));
        CHECK(encoded[1] == static_cast<std::byte>(0x7F));
        CHECK(encoded[2] == static_cast<std::byte>(0x80));
        CHECK(encoded[3] == static_cast<std::byte>(0x00));
        CHECK(encoded[4] == static_cast<std::byte>(0x00));
    }

    SUBCASE("NaN") {
        float value   = std::numeric_limits<float>::quiet_NaN();
        auto  encoded = encoder.serialize(value);
        fmt::print("NaN: ");
        print_bytes(encoded);
        CHECK(encoded.size() == 5);
        CHECK(encoded[0] == static_cast<std::byte>(0xFA));
        CHECK((encoded[1] & static_cast<std::byte>(0x7F)) == static_cast<std::byte>(0x7F));
        CHECK((encoded[2] & static_cast<std::byte>(0x80)) == static_cast<std::byte>(0x80));
        // The last two bytes can vary, so we don't check them
    }

    SUBCASE("Map of float sorted") {
        std::map<cbor::Value, cbor::Value> float_map;
        float_map.insert({3.0f, 3.14159f});
        float_map.insert({1.0f, 3.14159f});
        float_map.insert({2.0f, 3.14159f});
        float_map.insert({4.0f, 3.14159f});
        float_map.insert({-5.0f, 3.14159f});
        float_map.insert({-1.0f, 3.14159f});
        float_map.insert({10.0f, 3.14159f});
        // Mix in a double
        float_map.insert({3.0, 3.14159f});
        float_map.insert({-3.0, 3.14159f});

        // Mix in a string
        float_map.insert({"hello", 3.14159f});

        // Mix in a bool
        float_map.insert({true, 3.14159f});

        // Mix in a null
        float_map.insert({nullptr, 3.14159f});

        // Mix in some integers
        float_map.insert({1, 3.14159f});
        float_map.insert({-2, 3.14159f});
        float_map.insert({-3, 3.14159f});

        auto encoded = cbor::Encoder::serialize(float_map);
        fmt::print("Float map sorted: ");
        print_bytes(encoded);

        /*
            {
                -3: 3.14159_2,
                -2: 3.14159_2,
                1: 3.14159_2,
                "hello": 3.14159_2,
                -5.0_2: 3.14159_2,
                -1.0_2: 3.14159_2,
                1.0_2: 3.14159_2,
                2.0_2: 3.14159_2,
                3.0_2: 3.14159_2,
                4.0_2: 3.14159_2,
                10.0_2: 3.14159_2,
                -3.0_3: 3.14159_2,
                3.0_3: 3.14159_2,
                true: 3.14159_2,
                null: 3.14159_2,
            }
        */
        CHECK_EQ(to_hex(encoded),
                 "af22fa40490fd021fa40490fd001fa40490fd06568656c6c6ffa40490fd0fac0a00000fa40490fd0fabf800000fa40490fd0fa3f800000fa"
                 "40490fd0fa40000000fa40490fd0fa40400000fa40490fd0fa40800000fa40490fd0fa41200000fa40490fd0fbc008000000000000fa4049"
                 "0fd0fb4008000000000000fa40490fd0f5fa40490fd0f6fa40490fd0");
    }
}

TEST_CASE("CBOR Encoder - Double encoding") {
    cbor::Encoder encoder;

    SUBCASE("Positive double") {
        double value   = 3.14159265358979323846;
        auto   encoded = encoder.serialize(value);
        CHECK(encoded.size() == 9);
        CHECK(encoded[0] == static_cast<std::byte>(0xFB));
        CHECK(encoded[1] == static_cast<std::byte>(0x40));
        CHECK(encoded[2] == static_cast<std::byte>(0x09));
        CHECK(encoded[3] == static_cast<std::byte>(0x21));
        CHECK(encoded[4] == static_cast<std::byte>(0xFB));
        CHECK(encoded[5] == static_cast<std::byte>(0x54));
        CHECK(encoded[6] == static_cast<std::byte>(0x44));
        CHECK(encoded[7] == static_cast<std::byte>(0x2D));
        CHECK(encoded[8] == static_cast<std::byte>(0x18));
    }

    SUBCASE("Negative double") {
        double value   = -3.14159265358979323846;
        auto   encoded = encoder.serialize(value);
        fmt::print("Negative double: ");
        print_bytes(encoded);
        CHECK(encoded.size() == 9);
        CHECK(encoded[0] == static_cast<std::byte>(0xFB));
        CHECK(encoded[1] == static_cast<std::byte>(0xC0));
        CHECK(encoded[2] == static_cast<std::byte>(0x09));
        CHECK(encoded[3] == static_cast<std::byte>(0x21));
        CHECK(encoded[4] == static_cast<std::byte>(0xFB));
        CHECK(encoded[5] == static_cast<std::byte>(0x54));
        CHECK(encoded[6] == static_cast<std::byte>(0x44));
        CHECK(encoded[7] == static_cast<std::byte>(0x2D));
        CHECK(encoded[8] == static_cast<std::byte>(0x18));
    }

    SUBCASE("Zero") {
        double value   = 0.0;
        auto   encoded = encoder.serialize(value);
        CHECK(encoded.size() == 9);
        CHECK(encoded[0] == static_cast<std::byte>(0xFB));
        CHECK(encoded[1] == static_cast<std::byte>(0x00));
        CHECK(encoded[2] == static_cast<std::byte>(0x00));
        CHECK(encoded[3] == static_cast<std::byte>(0x00));
        CHECK(encoded[4] == static_cast<std::byte>(0x00));
        CHECK(encoded[5] == static_cast<std::byte>(0x00));
        CHECK(encoded[6] == static_cast<std::byte>(0x00));
        CHECK(encoded[7] == static_cast<std::byte>(0x00));
        CHECK(encoded[8] == static_cast<std::byte>(0x00));
    }

    SUBCASE("Infinity") {
        double value   = std::numeric_limits<double>::infinity();
        auto   encoded = encoder.serialize(value);
        CHECK(encoded.size() == 9);
        CHECK(encoded[0] == static_cast<std::byte>(0xFB));
        CHECK(encoded[1] == static_cast<std::byte>(0x7F));
        CHECK(encoded[2] == static_cast<std::byte>(0xF0));
        CHECK(encoded[3] == static_cast<std::byte>(0x00));
        CHECK(encoded[4] == static_cast<std::byte>(0x00));
        CHECK(encoded[5] == static_cast<std::byte>(0x00));
        CHECK(encoded[6] == static_cast<std::byte>(0x00));
        CHECK(encoded[7] == static_cast<std::byte>(0x00));
        CHECK(encoded[8] == static_cast<std::byte>(0x00));
    }

    SUBCASE("NaN") {
        double value   = std::numeric_limits<double>::quiet_NaN();
        auto   encoded = encoder.serialize(value);
        CHECK(encoded.size() == 9);
        CHECK(encoded[0] == static_cast<std::byte>(0xFB));
        CHECK((encoded[1] & static_cast<std::byte>(0x7F)) == static_cast<std::byte>(0x7F));
        CHECK((encoded[2] & static_cast<std::byte>(0xF0)) == static_cast<std::byte>(0xF0));
        // The last 6 bytes can vary, so we don't check them
    }
}

TEST_CASE("cbor decoder") {
    using namespace cbor;
    const char *data  = "a2676e756d626572734f8501026568656c6c6f03fa40800000656f746865724483f4f5f6";
    auto        bytes = to_bytes(data);

    SUBCASE("integers") {
        auto encoded = Encoder::serialize(std::uint64_t(4294967296));
        auto decoded = Decoder::deserialize(encoded);
        // REQUIRE(decoded);
        REQUIRE(std::holds_alternative<std::uint64_t>(decoded));
        CHECK_EQ(std::get<std::uint64_t>(decoded), 4294967296);

        encoded = Encoder::serialize(std::int64_t(-4294967296));
        decoded = Decoder::deserialize(encoded);
        // REQUIRE(decoded);
        REQUIRE(std::holds_alternative<std::int64_t>(decoded));
        CHECK_EQ(std::get<std::int64_t>(decoded), -4294967296);

        encoded = Encoder::serialize(std::int64_t(-42949672960));
        decoded = Decoder::deserialize(encoded);
        // REQUIRE(decoded);
        REQUIRE(std::holds_alternative<std::int64_t>(decoded));
        CHECK_EQ(std::get<std::int64_t>(decoded), -42949672960);

        // Check small negative
        encoded = Encoder::serialize(std::int64_t(-24));
        fmt::print("Small negative: {}\n", to_hex(encoded));
        decoded = Decoder::deserialize(encoded);
        // fmt::print("Small negative decoded: {}\n", std::get<std::int64_t>(decoded));

        // REQUIRE(decoded);
        REQUIRE(std::holds_alternative<std::int64_t>(decoded));
        CHECK_EQ(std::get<std::int64_t>(decoded), -24);
    }

    SUBCASE("Text strings") {
        auto encoded = Encoder::serialize(std::string_view("IETF"));
        auto decoded = Decoder::deserialize(encoded);
        // REQUIRE(decoded);
        REQUIRE(std::holds_alternative<std::string_view>(decoded));
        CHECK_EQ(std::get<std::string_view>(decoded), "IETF");

        encoded = Encoder::serialize(std::string("IETF"));
        decoded = Decoder::deserialize(encoded);
        // REQUIRE(decoded);
        REQUIRE(std::holds_alternative<std::string_view>(decoded));
        CHECK_EQ(std::get<std::string_view>(decoded), "IETF");
    }

    SUBCASE("Binary strings") {
        auto encoded = Encoder::serialize(std::span<std::byte>(bytes));
        auto decoded = Decoder::deserialize(encoded);
        // REQUIRE(decoded);
        REQUIRE(std::holds_alternative<std::span<const std::byte>>(decoded));
        // Loop and check each byte
        auto decoded_bytes = std::get<std::span<const std::byte>>(decoded);
        CHECK_EQ(decoded_bytes.size(), bytes.size());
        for (size_t i = 0; i < bytes.size(); ++i) {
            CHECK_EQ(decoded_bytes[i], bytes[i]);
        }
    }

    SUBCASE("Arrays") {
        auto encoded = Encoder::serialize(std::vector<Value>{1, 2, 3});
        auto decoded = Decoder::deserialize(encoded);
        REQUIRE(std::holds_alternative<ArrayView>(decoded));
        auto array         = std::get<ArrayView>(decoded);
        auto decoded_array = Decoder::deserialize(array);
        CHECK_EQ(decoded_array.size(), 3);
        CHECK_EQ(std::get<std::uint64_t>(decoded_array[0]), 1);
        CHECK_EQ(std::get<std::uint64_t>(decoded_array[1]), 2);
        CHECK_EQ(std::get<std::uint64_t>(decoded_array[2]), 3);
    }

    SUBCASE("Maps") {
        auto encoded = Encoder::serialize(std::map<Value, Value>{{"ca", 1}, {"ba", 2}, {"a", 3}});
        auto decoded = Decoder::deserialize(encoded);
        REQUIRE(std::holds_alternative<MapView>(decoded));

        auto map = Decoder::deserialize<std::map<Value, Value>>(std::get<MapView>(decoded));
        CHECK_EQ(map.size(), 3);
        CHECK_EQ(std::get<std::uint64_t>(map["ca"]), 1);
        CHECK_EQ(std::get<std::uint64_t>(map["ba"]), 2);
        CHECK_EQ(std::get<std::uint64_t>(map["a"]), 3);
    }

    SUBCASE("Binary string map") {
        auto decoded = Decoder::deserialize(std::span(bytes));
        REQUIRE(std::holds_alternative<MapView>(decoded));

        auto map = Decoder::deserialize<std::map<Value, Value>>(std::get<MapView>(decoded));
        CHECK_EQ(map.size(), 2);
        CHECK(map.contains("numbers"));
        CHECK(map.contains("other"));
    }

    SUBCASE("Floats unordered map") {
        auto umap = std::unordered_map<Value, Value>{{1.0f, 2.0f}, {3.0f, 4.0f}, {5.0, 6.0f}};

        auto encoded = Encoder::serialize(umap);
        fmt::print("Float map: {}\n", to_hex(encoded));
        auto decoded = Decoder::deserialize(encoded);
        REQUIRE(std::holds_alternative<MapView>(decoded));

        auto map = Decoder::deserialize<std::unordered_map<Value, Value>>(std::get<MapView>(decoded));

        CHECK_EQ(std::get<float>(map[1.0f]), 2.0f);
        CHECK_EQ(std::get<float>(map[3.0f]), 4.0f);
        CHECK_EQ(std::get<float>(map[5.0]), 6.0f);
        CHECK_THROWS_AS(std::get<double>(map[5.0]), std::exception);
    }

    SUBCASE("Doubles map") {
        auto encoded = Encoder::serialize(std::map<Value, Value>{{1.0, 2.0}, {3.0, 4.0}, {5.0, 6.0}});
        auto decoded = Decoder::deserialize(encoded);
        REQUIRE(std::holds_alternative<MapView>(decoded));

        auto map = Decoder::deserialize<std::map<Value, Value>>(std::get<MapView>(decoded));
        CHECK_EQ(map.size(), 3);
        CHECK_EQ(std::get<double>(map[1.0]), 2.0);
        CHECK_EQ(std::get<double>(map[3.0]), 4.0);
        CHECK_EQ(std::get<double>(map.at(5.0)), 6.0);
        CHECK_THROWS_AS(std::get<float>(map.at(5.0f)), std::exception);
    }

    SUBCASE("Double unordered map") {
        auto encoded = Encoder::serialize(std::unordered_map<Value, Value>{{1.0, 2.0}, {3.0, 4.0}, {5.0f, 6.0}});
        auto decoded = Decoder::deserialize(encoded);
        REQUIRE(std::holds_alternative<MapView>(decoded));

        auto s = std::format("{}", "decoded");

        auto map = Decoder::deserialize<std::unordered_map<Value, Value>>(std::get<MapView>(decoded));
        CHECK_EQ(map.size(), 3);
        CHECK_EQ(std::get<double>(map[1.0]), 2.0);
        CHECK_EQ(std::get<double>(map[3.0]), 4.0);
        CHECK_EQ(std::get<double>(map.at(5.0f)), 6.0);
        CHECK_THROWS_AS(std::get<double>(map.at(5.0)), std::out_of_range);
    }

    SUBCASE("Decode Simple values") {
        auto encoded = Encoder::serialize(true);
        auto decoded = Decoder::deserialize(encoded);
        REQUIRE(std::holds_alternative<bool>(decoded));
        CHECK_EQ(std::get<bool>(decoded), true);

        encoded = Encoder::serialize(false);
        decoded = Decoder::deserialize(encoded);
        REQUIRE(std::holds_alternative<bool>(decoded));
        CHECK_EQ(std::get<bool>(decoded), false);

        encoded = Encoder::serialize(nullptr);
        decoded = Decoder::deserialize(encoded);
        REQUIRE(std::holds_alternative<std::nullptr_t>(decoded));
    }
}