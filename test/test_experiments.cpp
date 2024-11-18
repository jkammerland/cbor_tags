
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

#include <algorithm>
#include <cstddef>
#include <deque>
#include <fmt/core.h>
#include <functional>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <ranges>
#include <sstream>
#include <type_traits>
#include <variant>
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

TEST_CASE("Assign string_view to string?") {
    {
        std::string_view sv = "Hello world!";
        std::string      s(sv);
        CHECK_EQ(s, "Hello world!");
    }
    {
        std::array<std::byte, 5>            arr;
        std::pmr::monotonic_buffer_resource r(arr.data(), arr.size(), std::pmr::get_default_resource());
        std::pmr::string                    s("Hello world!", &r);

        std::string s2(s);
        CHECK_EQ(s, "Hello world!");
    }
    {
        std::array<std::byte, 5>            arr;
        std::pmr::monotonic_buffer_resource r(arr.data(), arr.size(), std::pmr::get_default_resource());
        std::pmr::string                    s("Hello world!", &r);

        std::string_view sv(s);
        CHECK_EQ(sv, "Hello world!");
    }
    {
        // Use a std::range::view of a deque of chars, really long string
        std::deque<char> d;
        std::generate_n(std::back_inserter(d), 1000, [i = 0]() mutable { return 'a' + i++; });

        // Create a range view of the deque, as it is non-contiguous
        namespace views = std::ranges::views;
        auto view       = views::all(d);

        std::string s(view.begin(), view.end());
        CHECK(std::equal(s.begin(), s.end(), view.begin(), view.end()));
    }
}

// Helper to store type-function pairs
template <typename T, auto F> struct TypeFunction {
    using Type                     = T;
    static constexpr auto Function = F;
};

// Base handler class
template <typename... Handlers> class TypeHandler {
  public:
    template <typename T> static void handle(std::shared_ptr<T> ptr) {
        (try_handle<typename Handlers::Type>(ptr, Handlers::Function) || ...);
    }

  private:
    template <typename Target, typename T> static bool try_handle(std::shared_ptr<T> ptr, auto func) {
        if (auto derived = std::dynamic_pointer_cast<Target>(ptr)) {
            func(derived);
            return true;
        }
        return false;
    }
};

// Example usage:
class Base {
  public:
    virtual ~Base() = default;
};

class Derived1 : public Base {
  public:
    void method1() { std::cout << "Derived1::method1\n"; }
};

class Derived2 : public Base {
  public:
    void method2() { std::cout << "Derived2::method2\n"; }
};

// Define handlers
using MyHandler =
    TypeHandler<TypeFunction<Derived1, [](auto ptr) { ptr->method1(); }>, TypeFunction<Derived2, [](auto ptr) { ptr->method2(); }>>;

// Use the handler
template <typename T> void method(std::shared_ptr<T> ptr) { MyHandler::handle(ptr); }

TEST_CASE("TypeHandler test") {
    auto                  d1 = std::make_shared<Derived1>();
    auto                  d2 = std::make_shared<Derived2>();
    std::shared_ptr<Base> b1 = d1;
    std::shared_ptr<Base> b2 = d2;

    // Redirect std::cout to a stringstream to capture the output
    std::stringstream buffer;
    std::streambuf   *old = std::cout.rdbuf(buffer.rdbuf());

    method(d1); // Calls method1
    CHECK_EQ(buffer.str(), "Derived1::method1\n");
    buffer.str(""); // Clear the buffer

    method(d2); // Calls method2
    CHECK_EQ(buffer.str(), "Derived2::method2\n");
    buffer.str(""); // Clear the buffer

    method(b1); // Calls method1
    CHECK_EQ(buffer.str(), "Derived1::method1\n");
    buffer.str(""); // Clear the buffer

    method(b2); // Calls method2
    CHECK_EQ(buffer.str(), "Derived2::method2\n");

    // Restore the original std::cout buffer
    std::cout.rdbuf(old);
}
