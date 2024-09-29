
#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <bit>
#include <cstdint>
#include <doctest/doctest.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <nameof.hpp>
#include <string_view>
#include <sys/stat.h>
#include <system_error>
#include <variant>
#include <zpp_bits.h>

struct person {
    std::string                                                 name;
    int                                                         age;
    std::variant<std::monostate, std::string, std::vector<int>> data;
};

TEST_SUITE("zpp bits example") {
    TEST_CASE("serialize") {
        auto [data, in, out] = zpp::bits::data_in_out();
        fmt::print("data type: {}\n", nameof::nameof_type<decltype(data)>());
        fmt::print("in type: {}\n", nameof::nameof_type<decltype(in)>());
        fmt::print("out type: {}\n", nameof::nameof_type<decltype(out)>());

        auto result = out(person{"Person1", 25, {}}, person{"Person2", 35, std::vector<int>{1, 2, 3}});
        if (failure(result)) {
            // `result` is implicitly convertible to `std::errc`.
            // handle the error or return/throw exception.
            REQUIRE(false);
        }

        result = out(person{"Person4", 25, {}}, person{"Person5", 35, std::vector<int>{1, 2, 3}});
        if (failure(result)) {
            // `result` is implicitly convertible to `std::errc`.
            // handle the error or return/throw exception.
            REQUIRE(false);
        }

        person p1, p2;

        result = in(p1, p2);
        if (failure(result)) {
            // `result` is implicitly convertible to `std::errc`.
            // handle the error or return/throw exception.
        } else {
            CHECK(p1.name == "Person1");
            CHECK(p1.age == 25);
            CHECK(p2.name == "Person2");
            CHECK(p2.age == 35);
            CHECK(std::holds_alternative<std::monostate>(p1.data));
            CHECK(std::get<std::vector<int>>(p2.data) == std::vector<int>{1, 2, 3});
        }

        result = in(p1, p2);
        if (failure(result)) {
            // `result` is implicitly convertible to `std::errc`.
            // handle the error or return/throw exception.
        } else {
            CHECK(p1.name == "Person4");
            CHECK(p1.age == 25);
            CHECK(p2.name == "Person5");
            CHECK(p2.age == 35);
            CHECK(std::holds_alternative<std::monostate>(p1.data));
            CHECK(std::get<std::vector<int>>(p2.data) == std::vector<int>{1, 2, 3});
        }
    }
}

TEST_CASE("Serialize std::span") {
    auto [data, in, out] = zpp::bits::data_in_out(zpp::bits::endian::network{});

    std::vector<int> v{1, 2, 3, 4, 5};

    auto result = out(std::span(v));
    if (failure(result)) {
        // `result` is implicitly convertible to `std::errc`.
        // handle the error or return/throw exception.
        REQUIRE(false);
    }

    std::span<const std::byte> s;
    result = in(s);
    if (failure(result)) {
        // `result` is implicitly convertible to `std::errc`.
        // handle the error or return/throw exception.

        // Print error message
        fmt::print("Error: {}\n", int(result.code));
        REQUIRE(false);
    } else {
        CHECK(s.size() == 5);
        // CHECK(std::equal(v.begin(), v.end(), reinterpret_cast<const int *>(s.data())));
        fmt::print("s: {}\n", fmt::join(s, ", "));
    }
}

TEST_CASE("TAG example COSE") {
    auto [data, in, out] = zpp::bits::data_in_out();

    std::string data1 = "This is the content.";

    using namespace std::string_view_literals;
    auto result = out(cbor::tags::tag_view{16, std::span<std::byte>(reinterpret_cast<std::byte *>(data1.data()), data1.size())});

    if (failure(result)) {
        // `result` is implicitly convertible to `std::errc`.
        // handle the error or return/throw exception.
        REQUIRE(false);
    }

    cbor::tags::tag_view t;
    result = in(t);
    if (failure(result)) {
        // `result` is implicitly convertible to `std::errc`.
        // handle the error or return/throw exception.
        REQUIRE(false);
    } else {
        CHECK(t.tag == 16);
        CHECK(t.data.size() == data1.size());
        CHECK(std::equal(data1.begin(), data1.end(), reinterpret_cast<const char *>(t.data.data())));
    }
}

TEST_CASE("Tag with big endian encoding") {
    struct Double {
        double value;
    };

    auto [data, out]  = zpp::bits::data_out(zpp::bits::endian::big{});
    auto [dataIn, in] = zpp::bits::data_in(zpp::bits::endian::big{});

    Double d{3.14159};

    auto result = out(cbor::tags::make_tag(1, d));
    CHECK(!failure(result));

    // Transfer data
    dataIn = data;

    cbor::tags::tag_pair<Double> d2;
    result = in(d2);
    CHECK(!failure(result));
    CHECK(1 == d2.first);
    CHECK(d.value == d2.second.value);
}

struct DoubleStruct {
    double                         value;
    static constexpr std::uint64_t cbor_tag{1};
};

TEST_CASE("Tag struct with cbor_tag") {
    auto [data, out]  = zpp::bits::data_out();
    auto [dataIn, in] = zpp::bits::data_in();

    DoubleStruct d{3.14159};

    auto result = out(d);
    CHECK(!failure(result));

    // Transfer data
    CHECK(data.size() == 8);

    dataIn = data;

    DoubleStruct d2;
    result = in(d2);
    CHECK(!failure(result));
    CHECK(d.value == d2.value);
}

TEST_CASE("Tag string_view with cbor_tag") {
    auto [data, out]  = zpp::bits::data_out();
    auto [dataIn, in] = zpp::bits::data_in();

    // Data to transfer
    std::string_view s{"This is a string view"};

    // Encode data
    auto result = out(cbor::tags::make_tag(1, s));
    CHECK(!failure(result));

    // Emulate data transfer
    dataIn = data;

    // Decode data (I would like to use string_view as type here, not std::span<const std::byte>)
    cbor::tags::tag_pair<std::string_view> s2;
    result = in(s2);
    CHECK(!failure(result));

    // Check data
    CHECK(1 == s2.first);
    CHECK(s == std::string_view(reinterpret_cast<const char *>(s2.second.data()), s2.second.size()));

    // Check data location is different (it was truly transferred)
    CHECK(s.data() != reinterpret_cast<const char *>(s2.second.data()));
}

TEST_CASE("just string_views") {
    auto [data, out]  = zpp::bits::data_out();
    auto [dataIn, in] = zpp::bits::data_in();

    // Data to transfer
    std::string_view s{"This is a string view"};

    // Encode data
    auto result = out(s);
    CHECK(!failure(result));

    // Emulate data transfer
    dataIn = data;

    // Decode data (I would like to use string_view as type here, not std::span<const std::byte>)
    std::string_view s2;
    result = in(s2);
    CHECK(!failure(result));

    // Check data
    CHECK(s == s2);

    // Check data location is different (it was truly transferred)
    CHECK(s.data() != s2.data());

    CHECK(s2.data() >= reinterpret_cast<const char *>(dataIn.data()));
    CHECK(s2.data() < reinterpret_cast<const char *>(dataIn.data() + dataIn.size()));
}

TEST_CASE("Multiple string_views") {
    struct Data {
        std::string_view s1;
        std::string_view s2;
    };

    auto [data, out]  = zpp::bits::data_out();
    auto [dataIn, in] = zpp::bits::data_in();

    // Data to transfer
    Data d{"This is a string view", "This is another string view"};

    // Encode data
    auto result = out(d);
    CHECK(!failure(result));

    // Emulate data transfer
    dataIn = data;

    // Decode data (I would like to use string_view as type here, not std::span<const std::byte>)
    Data d2;
    result = in(d2);
    CHECK(!failure(result));

    // Check data
    CHECK(d.s1 == d2.s1);
    CHECK(d.s2 == d2.s2);

    // Check data location is different (it was truly transferred)
    CHECK(d.s1.data() != d2.s1.data());
    CHECK(d.s2.data() != d2.s2.data());

    CHECK(d2.s1.data() >= reinterpret_cast<const char *>(dataIn.data()));
    CHECK(d2.s1.data() < reinterpret_cast<const char *>(dataIn.data() + dataIn.size()));

    CHECK(d2.s2.data() >= reinterpret_cast<const char *>(dataIn.data()));
    CHECK(d2.s2.data() < reinterpret_cast<const char *>(dataIn.data() + dataIn.size()));
}

TEST_CASE("Mix string_view and std::span<const std::byte>") {
    struct Data {
        std::string_view           s1;
        std::span<const std::byte> s2;
        int                        a;
        double                     b;

        std::variant<float, double> v;
    };

    auto [data, out]  = zpp::bits::data_out();
    auto [dataIn, in] = zpp::bits::data_in();

    std::vector<std::byte> v{
        std::initializer_list<std::byte>{std::byte{0x1}, std::byte{0x2}, std::byte{0x3}, std::byte{0x4}, std::byte{0x5}}};

    // Data to transfer
    Data d{"This is a string view", std::span(v), 42, 3.14159, 3.14159f};

    // Encode data
    auto result = out(d);
    CHECK(!failure(result));

    // Emulate data transfer
    dataIn = data;

    // Decode data (I would like to use string_view as type here, not std::span<const std::byte>)
    Data d2;
    result = in(d2);
    CHECK(!failure(result));

    // Check data
    CHECK(d.s1 == d2.s1);
    CHECK(d.s2.size() == d2.s2.size());
    CHECK(std::equal(d.s2.begin(), d.s2.end(), d2.s2.begin()));

    // Check data location is different (it was truly transferred)
    CHECK(d.s1.data() != d2.s1.data());

    // pointer value is within the transferred data
    CHECK(d2.s1.data() >= reinterpret_cast<const char *>(dataIn.data()));
    CHECK(d2.s1.data() < reinterpret_cast<const char *>(dataIn.data() + dataIn.size()));

    CHECK(d2.s2.data() >= reinterpret_cast<const std::byte *>(dataIn.data()));
    CHECK(d2.s2.data() < reinterpret_cast<const std::byte *>(dataIn.data() + dataIn.size()));

    CHECK(d.a == d2.a);
    CHECK(d.b == d2.b);
    CHECK(std::get<float>(d.v) == std::get<float>(d2.v));
}

TEST_CASE("TEST endian") {
    using native = zpp::bits::endian::native;
    using big    = zpp::bits::endian::big;
    using little = zpp::bits::endian::little;

    static_assert(native::value == std::endian::native);
    static_assert(big::value == std::endian::big);
    static_assert(little::value == std::endian::little);
}