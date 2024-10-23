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
