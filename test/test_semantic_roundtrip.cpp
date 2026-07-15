#include "cbor_roundtrip.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace test_support = cbor::tags::test;

namespace {

enum class telemetry_state : std::uint8_t { starting, active, degraded };

struct diagnostic_snapshot {
    static constexpr std::uint64_t cbor_tag = 60100;

    std::uint32_t                           code{};
    std::string                             message;
    std::map<std::string, std::vector<int>> channels;

    bool operator==(const diagnostic_snapshot &) const = default;
};

using sample_value = std::variant<std::int64_t, std::string, bool>;

struct telemetry_sample {
    static constexpr std::uint64_t cbor_tag = 60101;

    std::string                   source;
    sample_value                  value;
    std::optional<std::string>    unit;
    std::vector<std::vector<int>> windows;

    bool operator==(const telemetry_sample &) const = default;
};

using telemetry_payload = std::variant<std::int64_t, std::string, std::vector<std::byte>, diagnostic_snapshot>;

struct telemetry_frame {
    std::uint64_t                                               sequence{};
    telemetry_state                                             state{};
    std::optional<std::string>                                  device_name;
    std::vector<telemetry_sample>                               samples;
    telemetry_payload                                           payload;
    std::map<std::string, std::variant<int, std::string, bool>> attributes;
    std::array<std::uint16_t, 3>                                calibration{};
    std::optional<diagnostic_snapshot>                          diagnostic;

    bool operator==(const telemetry_frame &) const = default;
};

telemetry_frame make_frame(telemetry_payload payload) {
    return {
        42,
        telemetry_state::degraded,
        std::string{"line-controller"},
        {
            {"temperature", std::int64_t{-12}, std::string{"C"}, {{1, 2, 3}, {4}}},
            {"mode", std::string{"maintenance"}, std::nullopt, {{5, 6}}},
            {"interlock", true, std::nullopt, {}},
        },
        std::move(payload),
        {
            {"attempt", 3},
            {"operator", std::string{"night-shift"}},
            {"verified", true},
        },
        {11, 22, 33},
        diagnostic_snapshot{7, "sensor drift", {{"raw", {8, 9}}, {"filtered", {10}}}},
    };
}

} // namespace

TEST_CASE("core roundtrips every payload alternative in a realistic aggregate") {
    SUBCASE("integer payload") {
        const auto input  = make_frame(std::int64_t{-9001});
        const auto output = test_support::roundtrip(input);
        CHECK(output == input);
    }

    SUBCASE("text payload") {
        const auto input  = make_frame(std::string{"manual override"});
        const auto output = test_support::roundtrip(input);
        CHECK(output == input);
    }

    SUBCASE("binary payload") {
        const auto input  = make_frame(std::vector<std::byte>{std::byte{0x00}, std::byte{0x7f}, std::byte{0xff}});
        const auto output = test_support::roundtrip(input);
        CHECK(output == input);
    }

    SUBCASE("tagged aggregate payload") {
        const auto input  = make_frame(diagnostic_snapshot{19, "over current", {{"phase-a", {1, 3, 5}}, {"phase-b", {2, 4, 6}}}});
        const auto output = test_support::roundtrip(input);
        CHECK(output == input);
    }
}

TEST_CASE("core roundtrips empty containers and disengaged optionals in aggregate composition") {
    telemetry_frame input{
        0, telemetry_state::starting, std::nullopt, {}, std::vector<std::byte>{}, {}, {0, 0, 0}, std::nullopt,
    };

    const auto output = test_support::roundtrip(input);
    CHECK(output == input);
}

TEST_CASE("core roundtrip replaces preseeded scalar optional and variant state") {
    const auto      input = make_frame(std::string{"fresh"});
    telemetry_frame output;
    output.sequence    = 999;
    output.state       = telemetry_state::active;
    output.device_name = std::string{"stale"};
    output.payload     = diagnostic_snapshot{999, "stale", {{"old", {99}}}};
    output.calibration = {99, 99, 99};
    output.diagnostic  = diagnostic_snapshot{998, "stale", {}};

    test_support::roundtrip_into(input, output);
    CHECK(output == input);
}
