#include "test_util.h"

#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace cbor::tags;

namespace {

enum class malformed_report_state : std::uint8_t { idle, active, degraded };

struct malformed_detail {
    static constexpr std::uint64_t cbor_tag = 60200;

    std::uint32_t                           code{};
    std::map<std::string, std::vector<int>> channels;

    bool operator==(const malformed_detail &) const = default;
};

struct malformed_event {
    static constexpr std::uint64_t cbor_tag = 60201;

    std::string                                   name;
    std::variant<std::int64_t, std::string, bool> value;
    std::optional<std::string>                    unit;
    std::vector<std::vector<int>>                 windows;

    bool operator==(const malformed_event &) const = default;
};

using malformed_payload = std::variant<std::int64_t, std::string, std::vector<std::byte>, malformed_detail>;

struct malformed_report {
    std::uint64_t                           sequence{};
    malformed_report_state                  state{};
    std::optional<std::string>              note;
    std::vector<malformed_event>            events;
    malformed_payload                       payload;
    std::map<std::string, std::vector<int>> history;
    std::array<std::uint16_t, 3>            calibration{};

    bool operator==(const malformed_report &) const = default;
};

struct incompatible_event {
    static constexpr std::uint64_t cbor_tag = malformed_event::cbor_tag;

    std::string                   name;
    std::vector<int>              value;
    std::optional<std::string>    unit;
    std::vector<std::vector<int>> windows;
};

struct wrong_tag_event {
    static constexpr std::uint64_t cbor_tag = malformed_event::cbor_tag + 1U;

    std::string                                   name;
    std::variant<std::int64_t, std::string, bool> value;
    std::optional<std::string>                    unit;
    std::vector<std::vector<int>>                 windows;
};

template <typename Event, typename Payload> struct report_source {
    std::uint64_t                           sequence{};
    malformed_report_state                  state{};
    std::optional<std::string>              note;
    std::vector<Event>                      events;
    Payload                                 payload;
    std::map<std::string, std::vector<int>> history;
    std::array<std::uint16_t, 3>            calibration{};
};

struct short_report_source {
    std::uint64_t                           sequence{};
    malformed_report_state                  state{};
    std::optional<std::string>              note;
    std::vector<malformed_event>            events;
    malformed_payload                       payload;
    std::map<std::string, std::vector<int>> history;
};

malformed_report make_report(malformed_payload payload, std::optional<std::string> note) {
    return {
        42,
        malformed_report_state::degraded,
        std::move(note),
        {
            {"temperature", std::int64_t{-12}, std::string{"C"}, {{1, 2, 3}, {4}}},
            {"mode", std::string{"maintenance"}, std::nullopt, {{5, 6}}},
            {"interlock", true, std::nullopt, {}},
        },
        std::move(payload),
        {{"recent", {1, 2, 3}}, {"older", {4}}},
        {11, 22, 33},
    };
}

void check_strict_prefixes(const malformed_report &input) {
    std::vector<std::byte> encoded;
    auto                   enc = make_encoder(encoded);
    REQUIRE(enc(input));

    for (std::size_t cut = 0; cut < encoded.size(); ++cut) {
        CAPTURE(cut);
        CAPTURE(encoded.size());

        {
            CAPTURE("contiguous");
            std::span<const std::byte> prefix{encoded.data(), cut};
            malformed_report           output;
            auto                       result = make_decoder(prefix)(output);

            CHECK_FALSE(result);
            if (!result) {
                CHECK_EQ(result.error(), status_code::incomplete);
            }
        }

        {
            CAPTURE("non-contiguous");
            std::deque<std::byte> prefix(encoded.begin(), encoded.begin() + static_cast<std::ptrdiff_t>(cut));
            malformed_report      output;
            auto                  result = make_decoder(prefix)(output);

            CHECK_FALSE(result);
            if (!result) {
                CHECK_EQ(result.error(), status_code::incomplete);
            }
        }
    }

    malformed_report output;
    auto             dec = make_decoder(encoded);
    REQUIRE(dec(output));
    REQUIRE(dec.tell() == encoded.end());
    CHECK(output == input);

    std::deque<std::byte> non_contiguous(encoded.begin(), encoded.end());
    malformed_report      non_contiguous_output;
    auto                  non_contiguous_dec = make_decoder(non_contiguous);
    REQUIRE(non_contiguous_dec(non_contiguous_output));
    REQUIRE(non_contiguous_dec.tell() == non_contiguous.cend());
    CHECK(non_contiguous_output == input);
}

struct malformed_case {
    std::string_view name;
    std::string_view hex;
    status_code      expected;
};

template <typename T, std::size_t N> void check_malformed_backends(const std::array<malformed_case, N> &cases) {
    for (const auto &test_case : cases) {
        CAPTURE(test_case.name);
        CAPTURE(test_case.hex);
        const auto bytes = to_bytes(test_case.hex);

        {
            CAPTURE("contiguous");
            T    output{};
            auto result = make_decoder(bytes)(output);
            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), test_case.expected);
        }

        {
            CAPTURE("non-contiguous");
            std::deque<std::byte> input(bytes.begin(), bytes.end());
            T                     output{};
            auto                  result = make_decoder(input)(output);
            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), test_case.expected);
        }
    }
}

template <typename Input> auto decode_report(const Input &input, malformed_report &output) {
    std::vector<std::byte> encoded;
    auto                   enc = make_encoder(encoded);
    REQUIRE(enc(input));

    auto dec = make_decoder(encoded);
    return dec(output);
}

} // namespace

TEST_CASE("complex aggregate variants reject every strict encoded prefix") {
    SUBCASE("integer payload") { check_strict_prefixes(make_report(std::int64_t{-9001}, std::string{"calibrated"})); }
    SUBCASE("text payload") { check_strict_prefixes(make_report(std::string{"manual override"}, std::nullopt)); }
    SUBCASE("binary payload") {
        check_strict_prefixes(
            make_report(std::vector<std::byte>{std::byte{0x00}, std::byte{0x7f}, std::byte{0xff}}, std::string{"binary"}));
    }
    SUBCASE("tagged aggregate payload") {
        check_strict_prefixes(make_report(malformed_detail{19, {{"phase-a", {1, 3}}, {"phase-b", {2, 4}}}}, std::nullopt));
    }
}

TEST_CASE("malformed structural matrix has contiguous and non-contiguous status parity") {
    SUBCASE("arrays") {
        constexpr std::array cases{
            malformed_case{"truncated definite array", "830102", status_code::incomplete},
            malformed_case{"indefinite array without break", "9f0102", status_code::incomplete},
        };
        check_malformed_backends<std::vector<int>>(cases);
    }

    SUBCASE("maps") {
        constexpr std::array cases{
            malformed_case{"truncated definite map value", "a101", status_code::incomplete},
            malformed_case{"indefinite map without break", "bf0102", status_code::incomplete},
            malformed_case{"break used as map value", "bf01ff", status_code::no_match_for_map_on_buffer},
        };
        check_malformed_backends<std::map<int, int>>(cases);
    }

    SUBCASE("byte strings") {
        constexpr std::array cases{
            malformed_case{"truncated definite byte string", "45010203", status_code::incomplete},
            malformed_case{"indefinite byte string without break", "5f4101", status_code::incomplete},
            malformed_case{"text chunk in byte string", "5f6161ff", status_code::no_match_for_bstr_on_buffer},
            malformed_case{"nested indefinite byte string", "5f5fff", status_code::no_match_for_bstr_on_buffer},
        };
        check_malformed_backends<std::vector<std::byte>>(cases);
    }

    SUBCASE("text strings") {
        constexpr std::array cases{
            malformed_case{"truncated definite text string", "65616263", status_code::incomplete},
            malformed_case{"indefinite text string without break", "7f6161", status_code::incomplete},
            malformed_case{"byte chunk in text string", "7f4101ff", status_code::no_match_for_tstr_on_buffer},
            malformed_case{"nested indefinite text string", "7f7fff", status_code::no_match_for_tstr_on_buffer},
        };
        check_malformed_backends<std::string>(cases);
    }

    SUBCASE("variants") {
        using target = std::variant<std::uint64_t, std::string, bool>;
        constexpr std::array cases{
            malformed_case{"truncated selected text alternative", "6261", status_code::incomplete},
            malformed_case{"unmatched array alternative", "80", status_code::no_match_in_variant_on_buffer},
        };
        check_malformed_backends<target>(cases);
    }

    SUBCASE("tags") {
        using target = tagged_object<static_tag<100>, int>;
        constexpr std::array cases{
            malformed_case{"tag without payload", "d864", status_code::incomplete},
            malformed_case{"wrong tag", "d86501", status_code::no_match_for_tag},
            malformed_case{"missing tag wrapper", "01", status_code::no_match_for_tag_on_buffer},
        };
        check_malformed_backends<target>(cases);
    }

    SUBCASE("aggregate envelopes") {
        constexpr std::array cases{
            malformed_case{"wrong aggregate size", "80", status_code::unexpected_group_size},
            malformed_case{"empty valid-size aggregate", "87", status_code::incomplete},
        };
        check_malformed_backends<malformed_report>(cases);
    }
}

TEST_CASE("typed valid inputs reject incompatible nested aggregate shapes") {
    const std::map<std::string, std::vector<int>> history{{"recent", {1, 2, 3}}};

    SUBCASE("nested event variant has no matching alternative") {
        report_source<incompatible_event, std::string> input{
            42,
            malformed_report_state::active,
            std::string{"calibrated"},
            {{"temperature", {1, 2, 3}, std::string{"C"}, {{1}, {2}}}},
            std::string{"ready"},
            history,
            {11, 22, 33},
        };
        malformed_report output;
        auto             result = decode_report(input, output);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_in_variant_on_buffer);
        CHECK(output.events.empty());
    }

    SUBCASE("nested event tag does not match") {
        report_source<wrong_tag_event, std::string> input{
            42,
            malformed_report_state::active,
            std::nullopt,
            {{"temperature", std::int64_t{-12}, std::string{"C"}, {{1}, {2}}}},
            std::string{"ready"},
            history,
            {11, 22, 33},
        };
        malformed_report output;
        auto             result = decode_report(input, output);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_tag);
        CHECK(output.events.empty());
    }

    SUBCASE("top-level payload variant has no matching alternative") {
        report_source<malformed_event, std::map<std::string, int>> input{
            42,
            malformed_report_state::active,
            std::nullopt,
            {{"interlock", true, std::nullopt, {}}},
            {{"unexpected", 1}},
            history,
            {11, 22, 33},
        };
        malformed_report output;
        auto             result = decode_report(input, output);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_in_variant_on_buffer);
        REQUIRE_EQ(output.events.size(), 1U);
        CHECK_EQ(output.events.front().name, "interlock");
    }

    SUBCASE("aggregate has too few fields") {
        short_report_source input{
            42, malformed_report_state::active, std::nullopt, {{"interlock", true, std::nullopt, {}}}, std::string{"ready"}, history};
        malformed_report output;
        auto             result = decode_report(input, output);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::unexpected_group_size);
        CHECK(output.events.empty());
    }
}
