#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <doctest/doctest.h>
#include <fmt/core.h>
#include <fmt/ranges.h>

using namespace cbor::tags;

TEST_CASE("CBOR Encoder - Positive float") {
    std::vector<std::byte> data;
    auto                   enc   = make_encoder(data);
    float                  value = 3.14159f;
    enc(value);
    fmt::print("Positive float: ");
    print_bytes(data);
    CHECK_EQ(to_hex(data), "fa40490fd0");

    auto  dec = make_decoder(data);
    float decoded;
    dec(decoded);
    CHECK_EQ(value, decoded);
}

TEST_CASE("CBOR Encoder - Negative float") {
    std::vector<std::byte> data;
    auto                   enc   = make_encoder(data);
    float                  value = -3.14159f;
    enc(value);
    fmt::print("Negative float: ");
    print_bytes(data);
    CHECK_EQ(to_hex(data), "fac0490fd0");
}

TEST_CASE("CBOR Encoder - Zero float") {
    std::vector<std::byte> data;
    auto                   enc   = make_encoder(data);
    float                  value = 0.0f;
    enc(value);
    fmt::print("Zero: ");
    print_bytes(data);
    CHECK_EQ(to_hex(data), "fa00000000");
}

TEST_CASE("CBOR Encoder - Infinity float") {
    std::vector<std::byte> data;
    auto                   enc   = make_encoder(data);
    float                  value = std::numeric_limits<float>::infinity();
    enc(value);
    fmt::print("Infinity: ");
    print_bytes(data);
    CHECK_EQ(to_hex(data), "fa7f800000");
}

TEST_CASE("CBOR Encoder - NaN float") {
    std::vector<std::byte> data;
    auto                   enc   = make_encoder(data);
    float                  value = std::numeric_limits<float>::quiet_NaN();
    enc(value);
    fmt::print("NaN: ");
    print_bytes(data);
    CHECK_EQ(to_hex(data), "fa7fc00000");
}

TEST_CASE("CBOR Encoder - Positive double") {
    std::vector<std::byte> data;
    auto                   enc   = make_encoder(data);
    double                 value = 3.14159265358979323846;
    enc(value);
    fmt::print("Positive double: ");
    print_bytes(data);
    CHECK_EQ(to_hex(data), "fb400921fb54442d18");
}

TEST_CASE("CBOR Encoder - Negative double") {
    std::vector<std::byte> data;
    auto                   enc   = make_encoder(data);
    double                 value = -3.14159265358979323846;
    enc(value);
    fmt::print("Negative double: ");
    print_bytes(data);
    CHECK_EQ(to_hex(data), "fbc00921fb54442d18");
}

TEST_CASE("CBOR Encoder - Zero double") {
    std::vector<std::byte> data;
    auto                   enc   = make_encoder(data);
    double                 value = 0.0;
    enc(value);
    fmt::print("Zero double: ");
    print_bytes(data);
    CHECK_EQ(to_hex(data), "fb0000000000000000");
}

TEST_CASE("CBOR Encoder - Infinity double") {
    std::vector<std::byte> data;
    auto                   enc   = make_encoder(data);
    double                 value = std::numeric_limits<double>::infinity();
    enc(value);
    fmt::print("Infinity double: ");
    print_bytes(data);
    CHECK_EQ(to_hex(data), "fb7ff0000000000000");
}

TEST_CASE("CBOR Encoder - NaN double") {
    std::vector<std::byte> data;
    auto                   enc   = make_encoder(data);
    double                 value = std::numeric_limits<double>::quiet_NaN();
    enc(value);
    fmt::print("NaN double: ");
    print_bytes(data);
    CHECK(data.size() == 9);
    CHECK(data[0] == static_cast<std::byte>(0xFB));
    CHECK((data[1] & static_cast<std::byte>(0x7F)) == static_cast<std::byte>(0x7F));
    CHECK((data[2] & static_cast<std::byte>(0xF0)) == static_cast<std::byte>(0xF0));
}