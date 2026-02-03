#include <doctest/doctest.h>

#include "someip/wire/someip.h"

#include <array>
#include <vector>

TEST_CASE("someip: header encode/decode and framing") {
    someip::wire::header h{};
    h.msg.service_id       = 0x1234;
    h.msg.method_id        = 0x5678;
    h.length               = 8; // header tail only, no payload, no TP
    h.req.client_id        = 0x9ABC;
    h.req.session_id       = 0xDEF0;
    h.protocol_version     = 1;
    h.interface_version    = 2;
    h.msg_type             = someip::wire::message_type::request;
    h.return_code          = 0;

    std::vector<std::byte> bytes;
    someip::wire::writer<std::vector<std::byte>> w{bytes};
    REQUIRE(someip::wire::encode_header(w, h).has_value());

    const std::vector<std::byte> expected = {
        std::byte{0x12}, std::byte{0x34}, std::byte{0x56}, std::byte{0x78}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x08},
        std::byte{0x9A}, std::byte{0xBC}, std::byte{0xDE}, std::byte{0xF0}, std::byte{0x01}, std::byte{0x02}, std::byte{0x00}, std::byte{0x00},
    };
    CHECK(bytes == expected);

    auto decoded = someip::wire::decode_header(std::span<const std::byte>(bytes.data(), bytes.size()));
    REQUIRE(decoded.has_value());
    CHECK(decoded->msg.service_id == 0x1234);
    CHECK(decoded->msg.method_id == 0x5678);
    CHECK(decoded->length == 8);
    CHECK(decoded->req.client_id == 0x9ABC);
    CHECK(decoded->req.session_id == 0xDEF0);
    CHECK(decoded->protocol_version == 1);
    CHECK(decoded->interface_version == 2);
    CHECK(decoded->msg_type == someip::wire::message_type::request);
    CHECK(decoded->return_code == 0);

    auto frame_size = someip::wire::frame_size_from_prefix(std::span<const std::byte>(bytes.data(), 8));
    REQUIRE(frame_size.has_value());
    CHECK(*frame_size == 16);
}
