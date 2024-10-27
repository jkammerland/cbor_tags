#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_reflection.h"
#include "doctest/doctest.h"

#include <functional>
#include <iostream>

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