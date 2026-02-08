#include <doctest/doctest.h>

#include "someip/iface/field.h"
#include "someip/sd/sd.h"
#include "someip/ser/config.h"
#include "someip/ser/decode.h"
#include "someip/types/union.h"
#include "someip/wire/endian.h"
#include "someip/wire/someip.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

static std::vector<std::byte> build_header_with_length(std::uint32_t length) {
    someip::wire::header h{};
    h.msg.service_id    = 0x1234;
    h.msg.method_id     = 0x0001;
    h.length            = length;
    h.req.client_id     = 0x0001;
    h.req.session_id    = 0x0002;
    h.protocol_version  = 1;
    h.interface_version = 1;
    h.msg_type          = someip::wire::message_type::request;
    h.return_code       = 0;

    std::vector<std::byte> out{};
    someip::wire::writer<std::vector<std::byte>> w{out};
    auto st = someip::wire::encode_header(w, h);
    REQUIRE(st.has_value());
    return out;
}

static void write_u32_be(std::vector<std::byte> &buf, std::size_t off, std::uint32_t v) {
    buf[off + 0] = std::byte((v >> 24) & 0xFFu);
    buf[off + 1] = std::byte((v >> 16) & 0xFFu);
    buf[off + 2] = std::byte((v >> 8) & 0xFFu);
    buf[off + 3] = std::byte((v >> 0) & 0xFFu);
}

static std::vector<std::byte> build_sd_frame_with_lengths(std::uint32_t entries_len, std::uint32_t options_len) {
    const auto payload_size = std::size_t{1u + 3u + 4u} + static_cast<std::size_t>(entries_len) + std::size_t{4u} +
                              static_cast<std::size_t>(options_len);
    const auto total_size = std::size_t{16u} + payload_size;

    std::vector<std::byte> frame(total_size, std::byte{0x00});

    someip::wire::header h{};
    h.msg.service_id       = someip::sd::kServiceId;
    h.msg.method_id        = someip::sd::kMethodId;
    h.length               = static_cast<std::uint32_t>(8u + payload_size);
    h.req.client_id        = 0;
    h.req.session_id       = 0;
    h.protocol_version     = 1;
    h.interface_version    = 1;
    h.msg_type             = someip::wire::message_type::notification;
    h.return_code          = someip::wire::return_code::E_OK;

    someip::wire::writer<std::vector<std::byte>> w{frame};
    auto st = someip::wire::encode_header(w, h);
    REQUIRE(st.has_value());

    const std::size_t payload_off = 16u;
    write_u32_be(frame, payload_off + 4u, entries_len);
    write_u32_be(frame, payload_off + 8u + entries_len, options_len);
    return frame;
}

} // namespace

TEST_CASE("someip: invalid header length") {
    auto frame = build_header_with_length(4);
    auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(frame.data(), frame.size()));
    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error() == someip::status_code::invalid_length);
}

TEST_CASE("someip: truncated frame") {
    auto frame = build_header_with_length(12); // total would be 20, but we only provide 16 bytes
    auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(frame.data(), frame.size()));
    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error() == someip::status_code::incomplete_frame);
}

TEST_CASE("someip: frame size prefix handles very large lengths without overflow wrap") {
    auto frame = build_header_with_length(0xFFFFFFFFu);

    auto total = someip::wire::frame_size_from_prefix(std::span<const std::byte>(frame.data(), 8));
    REQUIRE(total.has_value());
    CHECK(*total == static_cast<std::size_t>(0x1'0000'0007ULL));

    auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(frame.data(), frame.size()));
    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error() == someip::status_code::incomplete_frame);
}

TEST_CASE("someip: sd invalid option length") {
    someip::sd::service_entry_data e{};
    e.type          = someip::sd::entry_type::offer_service;
    e.service_id    = 0x1234;
    e.instance_id   = 0x0001;
    e.major_version = 1;
    e.ttl           = 5;
    e.minor_version = 0;

    someip::sd::ipv4_endpoint_option opt{};
    opt.discardable = false;
    opt.address     = {std::byte{127}, std::byte{0}, std::byte{0}, std::byte{1}};
    opt.l4_proto    = 0x06;
    opt.port        = 30509;
    opt.reserved    = 0;
    e.run1          = {someip::sd::option{opt}};

    someip::sd::packet_data pd{};
    pd.hdr.flags      = 0;
    pd.hdr.reserved24 = 0;
    pd.entries.push_back(someip::sd::entry_data{e});

    auto msg = someip::sd::encode_message(pd);
    REQUIRE(msg.has_value());
    auto frame = *msg;

    const std::size_t entries_len_offset = 16u + 4u; // payload start + flags/reserved
    REQUIRE(frame.size() >= entries_len_offset + 4u);
    write_u32_be(frame, entries_len_offset, 15u); // not a multiple of 16

    auto decoded = someip::sd::decode_message(std::span<const std::byte>(frame.data(), frame.size()));
    REQUIRE_FALSE(decoded.has_value());
    CHECK(decoded.error() == someip::status_code::sd_invalid_lengths);
}

TEST_CASE("someip: sd decode rejects oversized entry table before allocation blowup") {
    const auto entries_len = static_cast<std::uint32_t>((someip::sd::kDecodeMaxEntries + 1u) * 16u);
    auto       frame       = build_sd_frame_with_lengths(entries_len, 0u);

    auto decoded = someip::sd::decode_message(std::span<const std::byte>(frame.data(), frame.size()));
    REQUIRE_FALSE(decoded.has_value());
    CHECK(decoded.error() == someip::status_code::invalid_length);
}

TEST_CASE("someip: sd decode rejects oversized option table before allocation blowup") {
    const auto options_len = static_cast<std::uint32_t>((someip::sd::kDecodeMaxOptions + 1u) * 3u);
    auto       frame       = build_sd_frame_with_lengths(0u, options_len);

    auto decoded = someip::sd::decode_message(std::span<const std::byte>(frame.data(), frame.size()));
    REQUIRE_FALSE(decoded.has_value());
    CHECK(decoded.error() == someip::status_code::invalid_length);
}

TEST_CASE("someip: field helpers reject invalid descriptors in checked API") {
    someip::iface::field_descriptor invalid{};
    invalid.service_id       = 0x1234;
    invalid.setter_method_id = 0x0101;
    invalid.writable         = false;

    CHECK_FALSE(someip::iface::can_make_set_request(invalid));
    auto checked = someip::iface::try_make_set_request_header(invalid, someip::wire::request_id{0x1, 0x2}, 1);
    REQUIRE_FALSE(checked.has_value());
    CHECK(checked.error() == someip::status_code::error);

    auto legacy = someip::iface::make_set_request_header(invalid, someip::wire::request_id{0x1, 0x2}, 1);
    CHECK(legacy.msg_type == someip::wire::message_type::request);
    CHECK(legacy.msg.method_id == invalid.setter_method_id);
}

TEST_CASE("someip: invalid union selector") {
    using U = someip::types::union_variant<std::variant<std::monostate, std::uint8_t>, 8, 8, 0>;
    const someip::ser::config cfg{someip::wire::endian::big};

    std::array<std::byte, 3> buf{std::byte{0x01}, std::byte{0x02}, std::byte{0x00}}; // len=1, selector=2 (invalid)
    U out{};
    auto st = someip::ser::decode(std::span<const std::byte>(buf.data(), buf.size()), cfg, out, 0);
    REQUIRE_FALSE(st.has_value());
    CHECK(st.error() == someip::status_code::invalid_union_selector);
}
