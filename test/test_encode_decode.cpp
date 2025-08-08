
#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_integer.h"
#include "test_util.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <doctest/parts/doctest_fwd.h>
#include <fmt/base.h>
#include <fmt/core.h>
#include <forward_list>
#include <list>
#include <memory_resource>
#include <nameof.hpp>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

using namespace cbor::tags;

TEST_CASE_TEMPLATE("Roundtrip", T, std::vector<std::byte>, std::deque<std::byte>) {
    auto data = T{};
    auto enc  = make_encoder(data);

    std::vector<uint64_t> values(1e5);
    std::iota(values.begin(), values.end(), 0);
    for (const auto &value : values) {
        CHECK(enc(value));
    }

    // Emulate data transfer
    auto data_in = data;
    auto dec     = make_decoder(data_in);

    for (const auto &value : values) {
        std::variant<uint64_t, std::string> result;
        CHECK(dec(result));
        CHECK_EQ(std::holds_alternative<uint64_t>(result), true);
        CHECK_EQ(std::get<uint64_t>(result), static_cast<uint64_t>(value));
    }
}

TEST_CASE_TEMPLATE("Roundtrip binary cbor tagged array", T, std::vector<char>, std::deque<std::byte>, std::list<uint8_t>) {
    auto data = T{};
    auto enc  = make_encoder(data);

    std::vector<int> values(1e1);
    std::iota(values.begin(), values.end(), 0);

    auto t = make_tag_pair(static_tag<123>{}, values);
    CHECK(enc(t));

    auto dec    = make_decoder(data);
    auto result = make_tag_pair(static_tag<123>{}, std::vector<int>{});
    CHECK(dec(result));

    CHECK_EQ(result.second, values);
}

TEST_CASE("Decode just string in deque") {
    auto data = std::deque<char>{};
    auto enc  = make_encoder(data);

    std::string value = "Hello world";
    REQUIRE(enc(value));

    // fmt::print("{}\n", to_hex(data));

    std::string result;
    auto        dec = make_decoder(data);
    REQUIRE(dec(result));

    CHECK_EQ(value.size(), result.size());
    CHECK_EQ(result, value);
}

TEST_CASE_TEMPLATE("Decode array of strings", T, std::vector<char>, std::deque<char>, std::list<uint8_t>) {
    T data;
    if constexpr (HasReserve<T>) {
        data.reserve(1000);
    }
    auto enc = make_encoder(data);

    std::vector<std::string> values(10);
    std::generate(values.begin(), values.end(), [i = 0]() mutable { return fmt::format("Hello world {}", i++); });
    REQUIRE(enc(values));

    auto                     dec = make_decoder(data);
    std::vector<std::string> result;
    REQUIRE(dec(result));

    REQUIRE_EQ(values.size(), result.size());
    CHECK_EQ(std::equal(values.begin(), values.end(), result.begin()), true);
}

TEST_CASE_TEMPLATE("Decode tagged types", T, std::vector<std::byte>, std::deque<std::byte>, std::list<std::byte>) {
    T data;
    if constexpr (HasReserve<T>) {
        data.reserve(1000);
    }
    auto enc = make_encoder(data);

    struct A {
        int                        a;
        double                     b;
        std::optional<std::string> c;
        std::vector<int>           d;
    };

    using namespace cbor::tags::literals;
    CHECK(enc(make_tag_pair(123_tag, A{1, 3.14, "Hello", {1, 2, 3}})));

    auto hex = to_hex(data);
    fmt::print("to_hex: {}\n", hex);
    if (decltype(enc)::options::wrap_groups) {
        REQUIRE_EQ(hex, "d87b8401fb40091eb851eb851f6548656c6c6f83010203");
    }

    auto dec    = make_decoder(data);
    auto result = make_tag_pair(123_tag, A{});
    CHECK(dec(result));

    CHECK_EQ(result.second.a, 1);
    CHECK_EQ(result.second.b, 3.14);
    CHECK_EQ(result.second.c, "Hello");
    CHECK_EQ(result.second.d, std::vector<int>({1, 2, 3}));
}

TEST_CASE_TEMPLATE("Test optional types", T, int, double, std::string, std::variant<int, double>) {
    using namespace std::string_view_literals;
    using opt = std::optional<T>;
    {
        // Positive optional
        std::vector<std::byte> buffer1;
        auto                   enc = make_encoder(buffer1);
        opt                    optional;
        if constexpr (IsVariant<T>) {
            if (std::rand() % 2 == 0) {
                optional = static_cast<int>(std::rand() % 100000);
            } else {
                optional = static_cast<double>(std::rand() % 100000);
            }
        } else {
            optional = T{};
        }

        CHECK(enc(optional));
        auto buffer2 = buffer1;

        auto dec = make_decoder(buffer2);
        opt  result;
        CHECK(dec(result));

        CHECK_EQ(result.has_value(), true);
    }
    {
        // nullopt
        std::vector<std::byte> buffer1;
        auto                   enc      = make_encoder(buffer1);
        opt                    optional = std::nullopt;
        CHECK(enc(optional));

        auto buffer2 = buffer1;

        auto dec = make_decoder(buffer2);
        opt  result;
        CHECK(dec(result));

        CHECK_EQ(result.has_value(), false);
    }
}

TEST_CASE_TEMPLATE("Test int64_t input output", T, std::vector<std::byte>, std::deque<char>) {
    T data;
    if constexpr (HasReserve<T>) {
        data.reserve(1e3);
    }
    auto enc = make_encoder(data);

    std::vector<int64_t> values(1e3);
    std::iota(values.begin(), values.end(), 0);
    // make negative
    std::transform(values.begin(), values.end(), values.begin(), [](auto v) { return -v; });
    CHECK(enc(values));
    fmt::print("Data: {}\n", to_hex(data));

    auto                 dec = make_decoder(data);
    std::vector<int64_t> result;
    CHECK(dec(result));

    REQUIRE_EQ(values.size(), result.size());
    CHECK_EQ(values, result);
}

TEST_CASE_TEMPLATE("Test variant types", T, negative, double, std::string, std::variant<negative, double>) {
    using variant = std::variant<positive, T>;
    {
        std::vector<std::byte> buffer1;
        auto                   enc = make_encoder(buffer1);
        variant                v;
        v = static_cast<uint64_t>(std::rand() % 100000);
        CHECK(enc(v));

        auto buffer2 = buffer1;

        auto    dec = make_decoder(buffer2);
        variant result;
        CHECK(dec(result));
        CHECK_EQ(std::holds_alternative<positive>(result), true);
        CHECK_EQ(v, result);
    }

    {
        std::vector<std::byte> buffer1;
        auto                   enc = make_encoder(buffer1);
        T                      v;
        if constexpr (IsVariant<T>) {
            if (std::rand() % 2 == 0) {
                v = negative(std::rand() % 100000);
            } else {
                v = static_cast<double>(std::rand() % 100000);
            }
        } else if constexpr (std::is_same_v<T, int>) {
            v = static_cast<int>(-100);
        } else {
            v = T{};
        }
        CHECK(enc(v));
        fmt::print("Variant: {}\n", to_hex(buffer1));
        fmt::print("int64_t casted from int {}\n", static_cast<int64_t>(-100));

        auto buffer2 = buffer1;

        auto dec = make_decoder(buffer2);
        T    result;
        REQUIRE(dec(result));
        CHECK_EQ(v, result);
    }
}

TEST_CASE("Test strings and binary strings in std::variant") {
    using variant = std::variant<std::string, std::span<const std::byte>>;
    {
        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);
        variant                v;
        v = std::string("Hello world!");
        CHECK(enc(v));

        auto buffer2 = buffer;

        auto    dec = make_decoder(buffer2);
        variant result;
        CHECK(dec(result));
        CHECK_EQ(std::get<std::string>(v), std::get<std::string>(result));
        fmt::print("String: {}\n", std::get<std::string>(result));
    }
    {
        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);
        variant                v;
        auto                   vec = std::vector<std::byte>({std::byte(0x01), std::byte(0x02), std::byte(0x03)});
        v                          = std::span<std::byte>(vec);
        CHECK(enc(v));

        auto buffer2 = buffer;

        auto    dec = make_decoder(buffer2);
        variant result;
        CHECK(dec(result));
        for (size_t i = 0; i < vec.size(); ++i) {
            CHECK_EQ(std::get<std::span<const std::byte>>(v)[i], std::get<std::span<const std::byte>>(result)[i]);
        }
        fmt::print("Binary string: {}\n", to_hex(std::get<std::span<const std::byte>>(result)));
    }
}
