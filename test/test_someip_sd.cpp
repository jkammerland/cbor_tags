#include <doctest/doctest.h>

#include "someip/sd/sd.h"

#include <vector>

TEST_CASE("someip-sd: encode OfferService + IPv4 endpoint option and decode back") {
    someip::sd::ipv4_endpoint_option opt{};
    opt.discardable = false;
    opt.address = {std::byte{0xC0}, std::byte{0xA8}, std::byte{0x00}, std::byte{0x01}};
    opt.l4_proto = 0x11;
    opt.port     = 0x1234;
    opt.reserved = 0x00;

    someip::sd::service_entry_data e{};
    e.type          = someip::sd::entry_type::offer_service;
    e.service_id    = 0x1234;
    e.instance_id   = 0x0001;
    e.major_version = 0x02;
    e.ttl           = 0x00000A;
    e.minor_version = 0x00000005;
    e.run1          = {someip::sd::option{opt}};
    e.run2          = {};

    someip::sd::packet_data pd{};
    pd.hdr.flags     = 0x00;
    pd.hdr.reserved24 = 0x000000;
    pd.entries       = {someip::sd::entry_data{e}};

    auto msg = someip::sd::encode_message(pd);
    REQUIRE(msg.has_value());

    const std::vector<std::byte> expected = {
        // SOME/IP header
        std::byte{0xFF}, std::byte{0xFF}, std::byte{0x81}, std::byte{0x00}, // service+method
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x30}, // length = 48
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, // request id
        std::byte{0x01}, std::byte{0x01}, std::byte{0x02}, std::byte{0x00}, // pv, iv, msg_type, rc
        // SD payload
        std::byte{0x00},                         // flags
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, // reserved24
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x10}, // entries_len = 16
        // Entry (16 bytes)
        std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x10}, // type, idx1, idx2, counts
        std::byte{0x12}, std::byte{0x34}, std::byte{0x00}, std::byte{0x01}, // service, instance
        std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x0A}, // major, ttl
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x05}, // minor
        // options_len = 12
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x0C},
        // Option IPv4 endpoint (12 bytes)
        std::byte{0x00}, std::byte{0x09}, std::byte{0x04}, std::byte{0x00}, // len, type, discard
        std::byte{0xC0}, std::byte{0xA8}, std::byte{0x00}, std::byte{0x01}, // addr
        std::byte{0x11}, std::byte{0x12}, std::byte{0x34}, std::byte{0x00}, // proto, port, rsv
    };

    CHECK(*msg == expected);

    auto decoded = someip::sd::decode_message(std::span<const std::byte>(msg->data(), msg->size()));
    REQUIRE(decoded.has_value());
    CHECK(decoded->header.msg.service_id == someip::sd::kServiceId);
    CHECK(decoded->header.msg.method_id == someip::sd::kMethodId);
    CHECK(decoded->sd_payload.entries.size() == 1);
    CHECK(decoded->sd_payload.options.size() == 1);

    const auto &ent = decoded->sd_payload.entries[0];
    REQUIRE(std::holds_alternative<someip::sd::service_entry>(ent));
    const auto &se = std::get<someip::sd::service_entry>(ent);
    CHECK(se.c.type == someip::sd::entry_type::offer_service);
    CHECK(se.c.index1 == 0);
    CHECK(se.c.index2 == 0);
    CHECK(se.c.numopt1_numopt2 == 0x10);
    CHECK(se.c.service_id == 0x1234);
    CHECK(se.c.instance_id == 0x0001);
    CHECK(se.c.major_version == 0x02);
    CHECK(se.c.ttl == 0x00000A);
    CHECK(se.minor_version == 0x00000005);

    auto runs = someip::sd::resolve_option_runs(decoded->sd_payload, se.c);
    REQUIRE(runs.has_value());
    REQUIRE(runs->run1.size() == 1);
    CHECK(runs->run2.empty());

    REQUIRE(std::holds_alternative<someip::sd::ipv4_endpoint_option>(runs->run1[0]));
    const auto &opt2 = std::get<someip::sd::ipv4_endpoint_option>(runs->run1[0]);
    CHECK(opt2.discardable == false);
    CHECK(opt2.address[0] == std::byte{0xC0});
    CHECK(opt2.address[1] == std::byte{0xA8});
    CHECK(opt2.address[2] == std::byte{0x00});
    CHECK(opt2.address[3] == std::byte{0x01});
    CHECK(opt2.l4_proto == 0x11);
    CHECK(opt2.port == 0x1234);
}
