#include <doctest/doctest.h>

#include "someip/ser/decode.h"
#include "someip/ser/encode.h"
#include "someip/types/union.h"

#include <cstdint>
#include <variant>
#include <vector>

TEST_CASE("someip: union/variant encoding (no internal alignment)") {
    using V = std::variant<std::monostate, std::uint16_t, std::uint32_t>;
    using U = someip::types::union_variant<V, 32, 32, 0>;

    const someip::ser::config cfg{someip::wire::endian::big};

    U in{};
    in.value = std::uint16_t{0x1234};

    std::vector<std::byte> bytes{};
    REQUIRE(someip::ser::encode(bytes, cfg, in).has_value());

    const std::vector<std::byte> expected = {
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x02}, // len
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01}, // selector
        std::byte{0x12}, std::byte{0x34},                                   // payload
    };
    CHECK(bytes == expected);

    U out{};
    REQUIRE(someip::ser::decode(std::span<const std::byte>(bytes.data(), bytes.size()), cfg, out).has_value());
    REQUIRE(out.value.index() == 1);
    CHECK(std::get<1>(out.value) == 0x1234);
}

TEST_CASE("someip: union/variant internal padding is ignored on decode") {
    using V = std::variant<std::monostate, std::uint16_t, std::uint32_t>;
    using U = someip::types::union_variant<V, 32, 32, 32>;

    const someip::ser::config cfg{someip::wire::endian::big};

    U in{};
    in.value = std::uint16_t{0x1234};

    std::vector<std::byte> bytes{};
    REQUIRE(someip::ser::encode(bytes, cfg, in).has_value());

    const std::vector<std::byte> expected = {
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x04}, // len (payload+pad)
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01}, // selector
        std::byte{0x12}, std::byte{0x34},                                   // payload
        std::byte{0x00}, std::byte{0x00},                                   // pad to 4-byte alignment
    };
    CHECK(bytes == expected);

    // Mutate padding bytes to non-zero; decoder should still accept.
    bytes[10] = std::byte{0xAA};
    bytes[11] = std::byte{0xAA};

    U out{};
    REQUIRE(someip::ser::decode(std::span<const std::byte>(bytes.data(), bytes.size()), cfg, out).has_value());
    REQUIRE(out.value.index() == 1);
    CHECK(std::get<1>(out.value) == 0x1234);
}
