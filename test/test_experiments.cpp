#include "doctest/doctest.h"

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
