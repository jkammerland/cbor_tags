
#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_decoder.h"
#include "test_util.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <doctest/parts/doctest_fwd.h>
#include <fmt/core.h>
#include <list>
#include <memory_resource>
#include <nameof.hpp>
#include <optional>
#include <utility>
#include <variant>

using namespace cbor::tags;

TEST_CASE_TEMPLATE("CBOR Decoder", T, std::vector<char>, std::deque<std::byte>) {

    // using value_type = typename T::value_type;

    // std::vector<value_type> encoded;
    // encoded.push_back(value_type{0x01});
    // encoded.push_back(value_type{0x02});
    // encoded.push_back(value_type{0x03});

    // T    data(encoded.begin(), encoded.end());
    // auto dec = make_decoder<T>(data);

    // for (const auto &value : encoded) {
    //     auto result = dec.decode_value();
    //     CHECK_EQ(std::holds_alternative<uint64_t>(result), true);
    //     CHECK_EQ(std::get<uint64_t>(result), static_cast<uint64_t>(value));
    // }
}

TEST_CASE_TEMPLATE("CBOR decode from array", T, std::array<unsigned char, 5>, std::deque<char>) {
    // T data;
    // if constexpr (std::is_same_v<T, std::array<unsigned char, 5>>) {
    //     data = {0x01, 0x02, 0x03, 0x04, 0x05};
    // } else {
    //     data = {'\x01', '\x02', '\x03', '\x04', '\x05'};
    // }

    // auto dec = make_decoder(data);

    // for (const auto &value : data) {
    //     auto result = dec.decode_value();
    //     CHECK_EQ(std::holds_alternative<uint64_t>(result), true);
    //     CHECK_EQ(std::get<uint64_t>(result), static_cast<uint64_t>(value));
    // }
}

TEST_CASE_TEMPLATE("Test input tag 1", T, std::vector<uint8_t>, std::deque<uint8_t>, std::list<uint8_t>) {
    using namespace std::string_view_literals;
    auto bytes = to_bytes("016c48656c6c6f20776f726c6421"sv);

    auto dec = make_decoder(bytes);
    struct A {
        std::string b;
    };

    auto a               = make_tag_pair(tag<1>{}, A{});
    auto &[tag, value_a] = a;

    dec(a);
    CHECK_EQ(value_a.b, "Hello world!");
}

TEST_CASE_TEMPLATE("Test input tag 1 optional", T, std::vector<uint8_t>, std::deque<uint8_t>, std::list<uint8_t>) {
    {
        using namespace std::string_view_literals;
        auto bytes = to_bytes("016c48656c6c6f20776f726c6421"sv);

        auto dec = make_decoder(bytes);
        struct A {
            std::optional<std::string> b;
        };

        auto a               = make_tag_pair(tag<1>{}, A{});
        auto &[tag, value_a] = a;

        dec(a);
        CHECK_EQ(value_a.b, "Hello world!");
    }
    {
        using namespace std::string_view_literals;
        auto bytes = to_bytes("01f6"sv);

        auto dec = make_decoder(bytes);

        struct A {
            std::optional<std::string> b;
        };

        auto a               = make_tag_pair(tag<1>{}, A{});
        auto &[tag, value_a] = a;
        dec(a);
        CHECK_EQ(value_a.b, std::nullopt);
    }
}

template <typename MajorType, typename... T> bool contains_major(MajorType major, std::variant<T...>) {
    return (... || (major == ConceptType<MajorType, T>::value));
}

TEST_CASE_TEMPLATE("Test input tag 1 variant", T, std::vector<char>, std::deque<uint8_t>, std::list<std::byte>) {
    {
        using namespace std::string_view_literals;
        auto bytes = to_bytes("016c48656c6c6f20776f726c6421"sv);

        auto dec = make_decoder(bytes);
        struct A {
            std::variant<std::string, int> b;
        };

        auto a               = make_tag_pair(tag<1>{}, A{});
        auto &[tag, value_a] = a;
        value_a.b            = 4;

        REQUIRE(contains_major(static_cast<T::value_type>(3), value_a.b));

        dec(a);
        REQUIRE(std::holds_alternative<std::string>(value_a.b));
        CHECK_EQ(std::get<std::string>(value_a.b), "Hello world!");
    }
    {
        using namespace std::string_view_literals;
        auto bytes = to_bytes("01f6"sv);

        auto dec = make_decoder(bytes);

        struct A {
            std::optional<std::string> b;
        };

        auto a               = make_tag_pair(tag<1>{}, A{});
        auto &[tag, value_a] = a;
        dec(a);
        CHECK_EQ(value_a.b, std::nullopt);
    }
}