
/**
 * @file test_experiments.cpp
 *
 * @brief Test file exploring C++ language features for CBOR tags library implementation.
 * Contains experimental code testing:
 * - Multiple inheritance and function overloading with templates
 * - CRTP (Curiously Recurring Template Pattern) with mixins
 * - Virtual function handling and test suite structure
 * - Variant type membership checking
 *
 * This code serves as a playground for testing C++ features before implementing
 * them in the main CBOR tags library.
 */

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_reflection.h"
#include "doctest/doctest.h"

#include <cstddef>
#include <functional>
#include <iostream>
#include <vector>

struct featureA {
    constexpr int decode(double) { return 1; }
};

struct featureB {
    constexpr int decode(float) { return 2; }
};

template <typename... features> struct my_decoder : features... {
    using features::decode...;

    template <typename T> constexpr int decode(T) { return 0; }
};

TEST_CASE("feature test") {
    auto my_d = my_decoder<featureA, featureB>{};
    CHECK_EQ(my_d.decode(1.0), 1);
    CHECK_EQ(my_d.decode(2.0f), 2);
    CHECK_EQ(my_d.decode("Hello world!"), 0);

    auto my_d2 = my_decoder<>();
    CHECK_EQ(my_d2.decode(1.0), 0);
}

struct Test {
    virtual ~Test()    = default;
    virtual void run() = 0;
};

struct TEST_SUITE {
    struct test_iostream : Test {
        void run() override { std::cout << "Hello world!" << std::endl; }
    } a;

    struct test_doctest : Test {
        void run() override { std::cout << "Hello doctest!" << std::endl; }
    } b;
};

TEST_CASE("test suite") {
    auto tests = TEST_SUITE{};
    auto t     = cbor::tags::to_tuple(tests);
    std::apply([](auto &...args) { (args.run(), ...); }, t);
}

// Base template for CRTP
template <template <typename> typename... Mixins> struct base : Mixins<base<Mixins...>>... {
    using Mixins<base<Mixins...>>::decode...;

    template <typename T> constexpr int decode(T) { return 0; }

    int unique() { return 42; }
};

// Mixin templates
template <typename Derived> struct featureA_mixin {

    int methodInA() { return 33; }

    constexpr int decode(double) {
        auto &self = static_cast<Derived &>(*this);
        return 1 + self.unique();
    }
};

template <typename Derived> struct featureB_mixin : cbor::tags::crtp_base<Derived> {
    constexpr int decode(float) { return 2 + this->underlying().methodInA(); }
};

TEST_CASE("CRTP feature test") {
    auto my_d = base<featureA_mixin, featureB_mixin>{};
    CHECK_EQ(my_d.decode(1.0), 1 + 42);
    CHECK_EQ(my_d.decode(2.0f), 2 + 33);
    CHECK_EQ(my_d.decode("Hello world!"), 0);
}

#include <type_traits>
#include <variant>

// All possible types
struct Type1 {};
struct Type2 {};
struct Type3 {};
struct Type4 {};

// Helper to check if type is in variant
template <typename T, typename Variant> struct is_variant_member;

template <typename T, typename... Types>
struct is_variant_member<T, std::variant<Types...>> : std::bool_constant<(std::is_same_v<T, Types> || ...)> {};

template <typename T, typename Variant> inline constexpr bool is_variant_member_v = is_variant_member<T, Variant>::value;

TEST_CASE("Variant member check") {
    using var = std::variant<Type1, Type2, Type3>;
    static_assert(is_variant_member_v<Type1, var>);
    static_assert(is_variant_member_v<Type2, var>);
    static_assert(is_variant_member_v<Type3, var>);
    static_assert(!is_variant_member_v<Type4, var>);
}

// Parser for specific types
template <typename T, typename Buffer> T parse(Buffer &) { return T{}; }

using Buffer = std::vector<std::byte>;

template <typename... AllowedTypes> constexpr auto parseByID(std::variant<AllowedTypes...> &var, int typeID, Buffer &buf) -> void {
    using Variant = std::variant<AllowedTypes...>;

    switch (typeID) {
    case 1:
        if constexpr (is_variant_member_v<Type1, Variant>) {
            var = parse<Type1>(buf);
            return;
        }
        break;
    case 2:
        if constexpr (is_variant_member_v<Type2, Variant>) {
            var = parse<Type2>(buf);
            return;
        }
        break;
    case 3:
        if constexpr (is_variant_member_v<Type3, Variant>) {
            var = parse<Type3>(buf);
            return;
        }
        break;
    case 4:
        if constexpr (is_variant_member_v<Type4, Variant>) {
            var = parse<Type4>(buf);
            return;
        }
        break;
    default: break;
    }
    throw std::runtime_error("Invalid type ID or type not allowed in variant");
}

// Usage example
TEST_CASE("parseByID") {
    using Variant = std::variant<Type1, Type2>; // Only Type1 and Type2 allowed
    Buffer  buf;
    Variant val1;

    parseByID(val1, 1, buf);
    CHECK(std::holds_alternative<Type1>(val1));

    parseByID(val1, 2, buf);
    CHECK(std::holds_alternative<Type2>(val1));

    CHECK_THROWS_AS(parseByID(val1, 3, buf), std::runtime_error);
    CHECK_THROWS_AS(parseByID(val1, 4, buf), std::runtime_error);
    CHECK_THROWS_AS(parseByID(val1, 5, buf), std::runtime_error);
}