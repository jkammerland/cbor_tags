#include <doctest/doctest.h>

#include "someip/ser/config.h"
#include "someip/ser/decode.h"
#include "someip/types/padding.h"
#include "someip/wire/message.h"
#include "someip/wire/someip.h"
#include "someip/wire/tp.h"

#include <vector>

TEST_CASE("someip: TP header pack/unpack") {
    someip::wire::tp_header tp{};
    tp.offset_units_16B = 1;
    tp.reserved         = 0;
    tp.more_segments    = true;

    const auto packed = someip::wire::pack_tp_header(tp);
    CHECK(packed == 0x00000011u);

    const auto unpacked = someip::wire::unpack_tp_header(packed);
    CHECK(unpacked.offset_units_16B == 1);
    CHECK(unpacked.reserved == 0);
    CHECK(unpacked.more_segments == true);
}

TEST_CASE("someip: frame parsing with TP flag") {
    someip::wire::header h{};
    h.msg.service_id       = 0x1234;
    h.msg.method_id        = 0x0001;
    h.req.client_id        = 0x0001;
    h.req.session_id       = 0x0002;
    h.protocol_version     = 1;
    h.interface_version    = 1;
    h.msg_type             = static_cast<std::uint8_t>(someip::wire::message_type::tp_flag | someip::wire::message_type::request);
    h.return_code          = 0;

    someip::wire::tp_header tp{};
    tp.offset_units_16B = 0;
    tp.more_segments    = true;

    const std::vector<std::byte> payload = {std::byte{0xAA}, std::byte{0xBB}};
    h.length = static_cast<std::uint32_t>(8u + 4u + payload.size());

    std::vector<std::byte> frame{};
    someip::wire::writer<std::vector<std::byte>> w{frame};
    REQUIRE(someip::wire::encode_header(w, h).has_value());
    REQUIRE(someip::wire::encode_tp_header(w, tp).has_value());
    REQUIRE(w.write_bytes(std::span<const std::byte>(payload.data(), payload.size())).has_value());

    auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(frame.data(), frame.size()));
    REQUIRE(parsed.has_value());
    CHECK(parsed->hdr.length == h.length);
    REQUIRE(parsed->tp.has_value());
    CHECK(parsed->tp->offset_units_16B == 0);
    CHECK(parsed->tp->more_segments == true);
    CHECK(parsed->payload.size() == payload.size());
    CHECK(std::equal(parsed->payload.begin(), parsed->payload.end(), payload.begin(), payload.end()));
    CHECK(parsed->consumed == frame.size());
}

namespace {
struct aligned_tp_payload {
    someip::types::pad_to<64> pad{};
    std::uint32_t             value{};
};
} // namespace

TEST_CASE("someip: encode_message honors TP payload base offset for alignment") {
    const someip::ser::config cfg{someip::wire::endian::big};

    someip::wire::header h{};
    h.msg.service_id       = 0x1234;
    h.msg.method_id        = 0x0001;
    h.req.client_id        = 0x0001;
    h.req.session_id       = 0x0002;
    h.protocol_version     = 1;
    h.interface_version    = 1;
    h.msg_type             = static_cast<std::uint8_t>(someip::wire::message_type::tp_flag | someip::wire::message_type::request);
    h.return_code          = 0;

    someip::wire::tp_header tp{};
    tp.offset_units_16B = 0;
    tp.more_segments    = false;

    aligned_tp_payload payload{};
    payload.value = 0x11223344u;

    std::vector<std::byte> frame{};
    REQUIRE(someip::wire::encode_message(frame, h, cfg, payload, tp).has_value());

    auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(frame.data(), frame.size()));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->tp.has_value());
    CHECK(parsed->hdr.length == 20u); // 8 header-tail + 4 TP + 8 payload(aligned)
    CHECK(parsed->payload.size() == 8u);
    CHECK(frame.size() == 28u);

    aligned_tp_payload roundtrip{};
    REQUIRE(someip::ser::decode(parsed->payload, cfg, roundtrip, 20u).has_value());
    CHECK(roundtrip.value == payload.value);
}
