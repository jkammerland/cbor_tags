#include <doctest/doctest.h>

#include "someip/ser/decode.h"
#include "someip/ser/encode.h"

#include <cstdint>
#include <vector>

namespace {

struct ScalarPayload {
    std::uint16_t a{};
    std::int32_t  b{};
    bool          c{};
};

enum class E16 : std::uint16_t { v = 0x0102 };

struct EnumPayload {
    E16 e{};
};

} // namespace

TEST_CASE("someip: scalar payload big-endian") {
    const someip::ser::config cfg{someip::wire::endian::big};
    ScalarPayload             in{.a = 0x1234, .b = -2, .c = true};

    std::vector<std::byte> bytes{};
    REQUIRE(someip::ser::encode(bytes, cfg, in).has_value());

    const std::vector<std::byte> expected = {
        std::byte{0x12}, std::byte{0x34}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFE}, std::byte{0x01},
    };
    CHECK(bytes == expected);

    ScalarPayload out{};
    REQUIRE(someip::ser::decode(std::span<const std::byte>(bytes.data(), bytes.size()), cfg, out).has_value());
    CHECK(out.a == 0x1234);
    CHECK(out.b == -2);
    CHECK(out.c == true);
}

TEST_CASE("someip: scalar payload little-endian") {
    const someip::ser::config cfg{someip::wire::endian::little};
    ScalarPayload             in{.a = 0x1234, .b = -2, .c = true};

    std::vector<std::byte> bytes{};
    REQUIRE(someip::ser::encode(bytes, cfg, in).has_value());

    const std::vector<std::byte> expected = {
        std::byte{0x34}, std::byte{0x12}, std::byte{0xFE}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0x01},
    };
    CHECK(bytes == expected);

    ScalarPayload out{};
    REQUIRE(someip::ser::decode(std::span<const std::byte>(bytes.data(), bytes.size()), cfg, out).has_value());
    CHECK(out.a == 0x1234);
    CHECK(out.b == -2);
    CHECK(out.c == true);
}

TEST_CASE("someip: enum endianness") {
    {
        const someip::ser::config cfg{someip::wire::endian::big};
        EnumPayload              in{.e = E16::v};
        std::vector<std::byte>   bytes{};
        REQUIRE(someip::ser::encode(bytes, cfg, in).has_value());
        CHECK(bytes == std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}});
    }
    {
        const someip::ser::config cfg{someip::wire::endian::little};
        EnumPayload              in{.e = E16::v};
        std::vector<std::byte>   bytes{};
        REQUIRE(someip::ser::encode(bytes, cfg, in).has_value());
        CHECK(bytes == std::vector<std::byte>{std::byte{0x02}, std::byte{0x01}});
    }
}

TEST_CASE("someip: bool strict decode") {
    const someip::ser::config cfg{someip::wire::endian::big};
    bool                      out{};
    const std::vector<std::byte> bad = {std::byte{0x02}};
    auto st = someip::ser::decode(std::span<const std::byte>(bad.data(), bad.size()), cfg, out);
    CHECK_FALSE(st.has_value());
    CHECK(st.error() == someip::status_code::invalid_bool_value);
}
