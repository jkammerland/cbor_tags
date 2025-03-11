
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
#include "cbor_tags/cbor_concepts_checking.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_reflection.h"
#include "doctest/doctest.h"
#include "test_util.h"

#include <algorithm>
#include <array>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <fmt/core.h>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <memory_resource>
#include <nameof.hpp>
#include <numeric>
#include <ranges>
#include <set>
#include <sstream>
#include <type_traits>
#include <variant>
#include <vector>

using namespace cbor::tags;

std::string some_data = "Hello world!";
// COMPILER BUG IF YOU RENAME TO "A". Overlaps with another struct A in another TU.
// Should be a redefinition error, not a segfault in runtime because of wrong type used for deserialization.
// The definitions belong to seperate TUs as well...Only happens in debug, not release mode for some reason too.
struct A12398 {
    std::int64_t a;
    std::string  s;
};

// Helper function to print type and value
template <typename T> constexpr void print_type_and_value(const T &value) {
    if constexpr (std::is_same_v<T, A12398>) {
        fmt::print("Got type <A>: with values a={}, s={}\n", value.a, value.s);
    } else if constexpr (fmt::is_formattable<T>()) {
        fmt::print("Got type <{}> with value <{}>\n", nameof::nameof_short_type<T>(), value);
    } else {
        fmt::print("Got type <{}>\n", nameof::nameof_short_type<T>());
    }
}

TEST_CASE("Basic reflection") {
    struct M {
        int                a;
        std::optional<int> b;
    };

    auto count = detail::aggregate_binding_count<M>;
    fmt::print("M has {} members\n", count);
    CHECK_EQ(count, 2);

    auto is_constructible = IsBracesContructible<M, any, any>;
    fmt::print("M is braces construct with 2 members: {}\n", is_constructible);
    CHECK(is_constructible);

    // Check if we can construct M with any and any
    [[maybe_unused]] auto tmp1 = M{.a = any{}, .b = std::optional<int>{any{}}}; // This should compile
    [[maybe_unused]] auto tmp2 = M{.a = any{}, .b = any{}};                     // Question is if if we can compile this
}

TEST_CASE("Advanced reflection 0") {
    struct Z {
        int                        a;
        float                      b;
        std::string                c;
        std::vector<int>           d;
        std::map<std::string, int> e;
        std::deque<double>         f;

        A12398 g;

        std::optional<int>            h;
        std::optional<std::list<int>> i;
        std::vector<std::vector<int>> j;
        std::multimap<int, int>       k;
        std::set<std::pair<int, int>> l;
    };

    auto z = Z{.a = 42,
               .b = 3.14f,
               .c = "Hello world!",
               .d = {1, 2, 3},
               .e = {{"one", 1}, {"two", 2}, {"three", 3}},
               .f = {1.0, 2.0, 3.0},
               .g = A12398{.a = 42, .s = "Hello world!"},
               .h = std::nullopt,
               .i = std::list<int>{1, 2, 3},           // std::optional<std::list<int>>
               .j = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}}, // std::vector<std::vector<int>>
               .k = {{1, 2}, {1, 2}, {1, 2}},          // std::multimap<int, int>
               .l = {{1, 2}, {3, 4}, {5, 6}}};         // std::set<std::pair<int, int>>

    auto &&tuple = to_tuple(z);
    std::apply([](auto &&...args) { (print_type_and_value(args), ...); }, tuple);
    CHECK_EQ(detail::aggregate_binding_count<Z>, 12);
}

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

template <typename T> struct crtp_base {
    constexpr T       &underlying() { return static_cast<T &>(*this); }
    constexpr const T &underlying() const { return static_cast<const T &>(*this); }
};

template <typename Derived> struct featureB_mixin : ::crtp_base<Derived> {
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

template <typename T> struct OptionsExample {};

template <typename... T> constexpr bool IsDefault(OptionsExample<T...>) { return false; };

template <> constexpr bool IsDefault(OptionsExample<void>) { return true; }

TEST_CASE("OptionsExample") {
    CHECK(!IsDefault(OptionsExample<int>{}));
    CHECK(IsDefault(OptionsExample<void>{}));
}

TEST_CASE("Span and ranges from buffer") {
    auto data = std::array<std::byte, 10>{};
    for (int i = 0; i < 10; ++i) {
        data[i] = static_cast<std::byte>(i);
    }

    auto span = std::span(data);
    span[0]   = static_cast<std::byte>(42);
    CHECK_EQ(data[0], static_cast<std::byte>(42));

    auto range = std::ranges::subrange(data.begin(), data.end());

    CHECK(std::ranges::equal(range, data));

    auto begin = data.begin();
    auto end   = begin + 5;

    auto range2 = std::ranges::subrange(begin, end);
    CHECK(std::ranges::equal(range2, std::ranges::subrange(range.begin(), range.begin() + 5)));

    // assign range to deque
    std::deque<std::byte> d;
    std::ranges::copy(range, std::back_inserter(d));
    CHECK(std::ranges::equal(d, data));

    // assign range to vector
    std::vector<std::byte> v;
    std::ranges::copy(range, std::back_inserter(v));
    CHECK(std::ranges::equal(v, data));

    // Assign to a list of chars
    std::list<char> l;
    std::ranges::transform(range, std::back_inserter(l), [](std::byte b) { return static_cast<char>(b); });

    auto char_view = d | std::views::transform([](std::byte b) { return static_cast<char>(b); });
    CHECK(std::ranges::equal(l, char_view));
}

template <typename T, size_t N> constexpr int test_constexpr_array_if(std::array<T, N> arr, T val) {
    for (auto &v : arr) {
        if (v == val) {
            return 1;
        }
    }
    return 0;
}

TEST_CASE("constexpr test") {
    constexpr auto arr = std::array{0, 1, 2, 3};
    static_assert(test_constexpr_array_if(arr, 2) == 1);
    static_assert(test_constexpr_array_if(arr, 4) == 0);
}

struct NotDefaultConstructible {
    NotDefaultConstructible() = delete;
};

template <typename U, typename... T> constexpr bool is_in_variant(std::variant<T...>) { return (std::is_same_v<T, U> || ...); }
template <typename U, typename T> constexpr bool    is_in_variant() { return is_in_variant<U>(T{}); }

TEST_CASE("Is in variant") {
    using var = std::variant<int, float, double, NotDefaultConstructible>;
    static_assert(is_in_variant<int>(var{}));
    static_assert(is_in_variant<float>(var{}));
    static_assert(is_in_variant<double>(var{}));
    static_assert(is_in_variant<NotDefaultConstructible>(var{}));

    static_assert(!is_in_variant<char, var>());
    static_assert(!is_in_variant<short, var>());
}

template <typename... T> constexpr auto get_headers_in_pack() {
    // using Variant = std::variant<T...>;
    // constexpr auto no_ambigous_major_types_in_variant = valid_concept_mapping_v<Variant>;

    std::vector<size_t> headers{0, 1, 2, 3};
    headers.push_back(4);
    std::array<size_t, sizeof...(T)> result{};
    for (size_t i = 0; i < headers.size() && i < sizeof...(T); ++i) {
        result[i] = headers[i];
    }
    return result;
}

TEST_CASE("get_headers_in_pack overload") {
    // constexpr auto headers = get_headers_in_pack<int, float, double, std::string, std::vector<int>, std::map<int, int>>();
    // CHECK_EQ(headers.size(), 4);
}

constexpr auto compare_sizes() {
    auto a = std::array<int, 5>{};
    auto b = std::vector{1, 2, 3, 4, 5};
    for (size_t i = 0; i < b.size(); ++i) {
        a[i] = b[i];
    }
    return a;
}

TEST_CASE("Get matching tags") {
    constexpr auto a = compare_sizes();
    static_assert(a.size() == 5);
    static_assert(a[0] == 1);
    static_assert(a[1] == 2);
    static_assert(a[2] == 3);
}

TEST_CASE("Nested struct without tags") {
    struct A {
        struct B {
            struct C {
                int a;
            } c;
        } b;
    };

    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);

    A a{.b = {.c = {.a = 42}}};
    enc(a);

    auto dec = make_decoder(data);
    A    result;
    dec(result);

    CHECK_EQ(result.b.c.a, 42);
}

auto switch_to_new_thread(std::jthread &out) {
    struct awaitable {
        std::jthread *p_out;
        bool          await_ready() { return false; }
        void          await_suspend(std::coroutine_handle<> h) {
            std::jthread &out = *p_out;
            if (out.joinable())
                throw std::runtime_error("Output jthread parameter not empty");
            out = std::jthread([h] { h.resume(); });
            // Potential undefined behavior: accessing potentially destroyed *this
            // std::cout << "New thread ID: " << p_out->get_id() << '\n';
            std::cout << "New thread ID: " << out.get_id() << '\n'; // this is OK
        }
        void await_resume() {}
    };
    return awaitable{&out};
}

struct task {
    struct promise_type {
        task               get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void               return_void() {}
        void               unhandled_exception() {}
    };
};

task resuming_on_new_thread(std::jthread &out) {
    std::cout << "Coroutine started on thread: " << std::this_thread::get_id() << '\n';
    co_await switch_to_new_thread(out);
    // awaiter destroyed here
    std::cout << "Coroutine resumed on thread: " << std::this_thread::get_id() << '\n';
}

TEST_CASE("Coroutine test") {
    std::jthread out;
    resuming_on_new_thread(out);
    out.join();
}

template <typename promise> class coroutine {
    std::coroutine_handle<promise> handle_;

  public:
    using promise_type = promise;

    coroutine(std::coroutine_handle<promise_type> handle) : handle_(handle) {}
    ~coroutine() {
        if (handle_) {
            handle_.destroy();
        }
    }

    coroutine(const coroutine &)            = delete;
    coroutine &operator=(const coroutine &) = delete;

    coroutine(coroutine &&other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    coroutine &operator=(coroutine &&other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }

            handle_       = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    bool done() const { return handle_.done(); }
    auto resume() { handle_.resume(); }
};

template <typename T> struct promise1 {
    T                   result; // Add this to store the returned value
    auto                get_return_object() { return std::coroutine_handle<std::remove_cvref_t<decltype(*this)>>::from_promise(*this); }
    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void                return_value(T i) { result = i; } // Changed to void return type
    void                unhandled_exception() { std::rethrow_exception(std::current_exception()); }
};

coroutine<promise1<int>> async_p1(int i) { co_return i; }

TEST_CASE("Coroutine guard test") {
    auto c1 = async_p1(1);
    c1.resume();
    CHECK(c1.done());
}