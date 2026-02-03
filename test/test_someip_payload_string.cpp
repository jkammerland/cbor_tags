#include <doctest/doctest.h>

#include "someip/ser/decode.h"
#include "someip/ser/encode.h"
#include "someip/types/string.h"

#include <vector>

TEST_CASE("someip: UTF-8 string (AUTOSAR strict)") {
    const someip::ser::config cfg{someip::wire::endian::big};
    someip::types::utf8_string<32> in{};
    in.value = "Hi";

    std::vector<std::byte> bytes{};
    REQUIRE(someip::ser::encode(bytes, cfg, in).has_value());

    const std::vector<std::byte> expected = {
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x06}, // length
        std::byte{0xEF}, std::byte{0xBB}, std::byte{0xBF},                 // BOM
        std::byte{0x48}, std::byte{0x69},                                   // "Hi"
        std::byte{0x00},                                                    // NUL
    };
    CHECK(bytes == expected);

    someip::types::utf8_string<32> out{};
    REQUIRE(someip::ser::decode(std::span<const std::byte>(bytes.data(), bytes.size()), cfg, out).has_value());
    CHECK(out.value == "Hi");
}

TEST_CASE("someip: UTF-16 string (little-endian, BOM matches payload endian)") {
    const someip::ser::config cfg{someip::wire::endian::little};
    someip::types::utf16_string<32> in{};
    in.value = u"Hi";

    std::vector<std::byte> bytes{};
    REQUIRE(someip::ser::encode(bytes, cfg, in).has_value());

    const std::vector<std::byte> expected = {
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x08}, // length
        std::byte{0xFF}, std::byte{0xFE},                                   // BOM (LE)
        std::byte{0x48}, std::byte{0x00},                                   // 'H'
        std::byte{0x69}, std::byte{0x00},                                   // 'i'
        std::byte{0x00}, std::byte{0x00},                                   // terminator
    };
    CHECK(bytes == expected);

    someip::types::utf16_string<32> out{};
    REQUIRE(someip::ser::decode(std::span<const std::byte>(bytes.data(), bytes.size()), cfg, out).has_value());
    CHECK(out.value == u"Hi");
}

TEST_CASE("someip: UTF-8 string invalid termination") {
    const someip::ser::config cfg{someip::wire::endian::big};
    const std::vector<std::byte> bytes = {
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x05}, // length
        std::byte{0xEF}, std::byte{0xBB}, std::byte{0xBF},                 // BOM
        std::byte{0x48}, std::byte{0x69},                                   // missing NUL
    };
    someip::types::utf8_string<32> out{};
    auto st = someip::ser::decode(std::span<const std::byte>(bytes.data(), bytes.size()), cfg, out);
    CHECK_FALSE(st.has_value());
    CHECK(st.error() == someip::status_code::invalid_string_termination);
}

TEST_CASE("someip: UTF-16 string odd length rejected") {
    const someip::ser::config cfg{someip::wire::endian::little};
    const std::vector<std::byte> bytes = {
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x07}, // length (odd)
        std::byte{0xFF}, std::byte{0xFE},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    };
    someip::types::utf16_string<32> out{};
    auto st = someip::ser::decode(std::span<const std::byte>(bytes.data(), bytes.size()), cfg, out);
    CHECK_FALSE(st.has_value());
    CHECK(st.error() == someip::status_code::invalid_utf16);
}
