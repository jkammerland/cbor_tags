#include "someip/sd/sd.h"
#include "someip/wire/someip.h"

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace {

constexpr std::size_t kMaxFrameBytes = 4096u;

std::vector<std::byte> to_bytes(const std::vector<std::uint8_t> &raw) {
    std::vector<std::byte> out{};
    out.reserve(raw.size());
    for (const auto value : raw) {
        out.push_back(static_cast<std::byte>(value));
    }
    return out;
}

std::array<std::byte, 8> to_bytes(const std::array<std::uint8_t, 8> &raw) {
    std::array<std::byte, 8> out{};
    for (std::size_t i = 0; i < raw.size(); ++i) {
        out[i] = static_cast<std::byte>(raw[i]);
    }
    return out;
}

std::uint32_t read_u32_be(const std::array<std::uint8_t, 8> &prefix) {
    return (std::uint32_t{prefix[4]} << 24) | (std::uint32_t{prefix[5]} << 16) | (std::uint32_t{prefix[6]} << 8) |
           std::uint32_t{prefix[7]};
}

void SomeIpTryParseFramePreservesInvariants(const std::vector<std::uint8_t> &raw_frame) {
    const auto frame = to_bytes(raw_frame);
    const auto parsed = someip::wire::try_parse_frame(std::span<const std::byte>(frame.data(), frame.size()));
    if (!parsed) {
        return;
    }

    const bool has_tp = someip::wire::has_tp_flag(parsed->hdr);
    const auto tp_bytes = has_tp ? std::size_t{4u} : std::size_t{0u};

    ASSERT_LE(parsed->consumed, frame.size());
    EXPECT_EQ(parsed->consumed, static_cast<std::size_t>(parsed->hdr.length) + 8u);
    EXPECT_EQ(parsed->tp.has_value(), has_tp);
    ASSERT_GE(parsed->hdr.length, 8u + tp_bytes);
    EXPECT_EQ(parsed->payload.size(), static_cast<std::size_t>(parsed->hdr.length - 8u - tp_bytes));
}

FUZZ_TEST(SomeIpWireFuzz, SomeIpTryParseFramePreservesInvariants)
    .WithDomains(fuzztest::Arbitrary<std::vector<std::uint8_t>>().WithMaxSize(kMaxFrameBytes));

void SomeIpFrameSizeFromPrefixMatchesLengthField(const std::array<std::uint8_t, 8> &prefix_raw) {
    const auto prefix = to_bytes(prefix_raw);
    const auto total = someip::wire::frame_size_from_prefix(std::span<const std::byte>(prefix.data(), prefix.size()));

    const auto length = read_u32_be(prefix_raw);
    if (length < 8u) {
        ASSERT_FALSE(total.has_value());
        EXPECT_EQ(total.error(), someip::status_code::invalid_length);
        return;
    }

    ASSERT_TRUE(total.has_value());
    EXPECT_EQ(*total, static_cast<std::size_t>(length) + 8u);
}

FUZZ_TEST(SomeIpWireFuzz, SomeIpFrameSizeFromPrefixMatchesLengthField);

void SomeIpSdDecodeArbitraryFrameNoCrash(const std::vector<std::uint8_t> &raw_frame) {
    const auto frame = to_bytes(raw_frame);
    const auto decoded = someip::sd::decode_message(std::span<const std::byte>(frame.data(), frame.size()));
    if (!decoded) {
        return;
    }

    EXPECT_EQ(decoded->header.msg.service_id, someip::sd::kServiceId);
    EXPECT_EQ(decoded->header.msg.method_id, someip::sd::kMethodId);
    EXPECT_EQ(decoded->header.interface_version, 1);
    EXPECT_EQ(decoded->header.msg_type, someip::wire::message_type::notification);

    for (const auto &entry : decoded->sd_payload.entries) {
        (void)std::visit(
            [&](const auto &typed_entry) { return someip::sd::resolve_option_runs(decoded->sd_payload, typed_entry.c); },
            entry);
    }
}

FUZZ_TEST(SomeIpSdFuzz, SomeIpSdDecodeArbitraryFrameNoCrash)
    .WithDomains(fuzztest::Arbitrary<std::vector<std::uint8_t>>().WithMaxSize(kMaxFrameBytes));

void SomeIpSdEncodeDecodeRoundTripSingleServiceEntry(std::uint16_t                    client_id,
                                                     std::uint16_t                    session_id,
                                                     std::uint16_t                    service_id,
                                                     std::uint16_t                    instance_id,
                                                     std::uint8_t                     major_version,
                                                     std::uint32_t                    ttl_raw,
                                                     std::uint32_t                    minor_version,
                                                     bool                             discardable,
                                                     const std::vector<std::uint8_t> &config_payload_raw) {
    someip::sd::packet_data pd{};
    pd.client_id = client_id;
    pd.session_id = session_id;

    const auto entry_type =
        (minor_version & 1u) != 0u ? someip::sd::entry_type::find_service : someip::sd::entry_type::offer_service;
    const auto ttl = ttl_raw & 0xFFFFFFu;

    someip::sd::service_entry_data entry{};
    entry.type = entry_type;
    entry.service_id = service_id;
    entry.instance_id = instance_id;
    entry.major_version = major_version;
    entry.ttl = ttl;
    entry.minor_version = minor_version;

    someip::sd::configuration_option option{};
    option.discardable = discardable;
    option.bytes = to_bytes(config_payload_raw);
    entry.run1.push_back(someip::sd::option{option});

    pd.entries.push_back(someip::sd::entry_data{entry});

    const auto encoded = someip::sd::encode_message(pd);
    ASSERT_TRUE(encoded.has_value());

    const auto decoded = someip::sd::decode_message(std::span<const std::byte>(encoded->data(), encoded->size()));
    ASSERT_TRUE(decoded.has_value());

    EXPECT_EQ(decoded->header.req.client_id, client_id);
    EXPECT_EQ(decoded->header.req.session_id, session_id);
    ASSERT_EQ(decoded->sd_payload.entries.size(), 1u);
    ASSERT_EQ(decoded->sd_payload.options.size(), 1u);

    const auto *decoded_entry = std::get_if<someip::sd::service_entry>(&decoded->sd_payload.entries[0]);
    ASSERT_NE(decoded_entry, nullptr);
    EXPECT_EQ(decoded_entry->c.type, entry_type);
    EXPECT_EQ(decoded_entry->c.service_id, service_id);
    EXPECT_EQ(decoded_entry->c.instance_id, instance_id);
    EXPECT_EQ(decoded_entry->c.major_version, major_version);
    EXPECT_EQ(decoded_entry->c.ttl, ttl);
    EXPECT_EQ(decoded_entry->minor_version, minor_version);

    const auto runs = someip::sd::resolve_option_runs(decoded->sd_payload, decoded_entry->c);
    ASSERT_TRUE(runs.has_value());
    ASSERT_EQ(runs->run1.size(), 1u);
    ASSERT_TRUE(runs->run2.empty());

    const auto *decoded_option = std::get_if<someip::sd::configuration_option>(&runs->run1[0]);
    ASSERT_NE(decoded_option, nullptr);
    EXPECT_EQ(decoded_option->discardable, discardable);
    EXPECT_EQ(decoded_option->bytes, option.bytes);
}

FUZZ_TEST(SomeIpSdFuzz, SomeIpSdEncodeDecodeRoundTripSingleServiceEntry)
    .WithDomains(fuzztest::Arbitrary<std::uint16_t>(), fuzztest::Arbitrary<std::uint16_t>(),
                 fuzztest::Arbitrary<std::uint16_t>(), fuzztest::Arbitrary<std::uint16_t>(),
                 fuzztest::Arbitrary<std::uint8_t>(), fuzztest::Arbitrary<std::uint32_t>(),
                 fuzztest::Arbitrary<std::uint32_t>(), fuzztest::Arbitrary<bool>(),
                 fuzztest::Arbitrary<std::vector<std::uint8_t>>().WithMaxSize(128u));

} // namespace
