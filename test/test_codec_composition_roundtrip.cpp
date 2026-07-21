#include "cbor_roundtrip.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/custom_codec_1.h>
#include <cbor_tags/extensions/rfc8746_typed_arrays.h>
#include <cbor_tags/extensions/smart_ptr.h>

#if __has_include(<version>)
#include <version>
#endif

#if __has_include(<expected>) && defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
#include <cbor_tags/extensions/std_expected.h>
#include <expected>
#define CBOR_TAGS_TEST_HAS_STD_EXPECTED 1
#endif

#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace cbor::tags;
using namespace cbor::tags::ext::custom_codec_1;
using namespace cbor::tags::ext::rfc8746;
using namespace cbor::tags::ext::smart_ptr;
namespace test_support = cbor::tags::test;

#ifdef CBOR_TAGS_TEST_HAS_STD_EXPECTED
using namespace cbor::tags::ext::std_expected;
#endif

namespace {

enum class codec_report_state : std::uint8_t { idle, active, degraded };

struct compact_codec_event {
    std::string                                   name;
    std::variant<std::int64_t, std::string, bool> value;
    std::optional<std::string>                    unit;
    std::vector<std::vector<int>>                 windows;

    bool operator==(const compact_codec_event &) const = default;
};

struct compact_codec_report {
    codec_report_state                                state{};
    std::optional<std::string>                        note;
    std::vector<compact_codec_event>                  events;
    std::variant<std::string, std::vector<int>, bool> result;
    std::map<std::string, std::vector<int>>           history;

    bool operator==(const compact_codec_report &) const = default;
};

struct codec_owner {
    std::uint64_t            id{};
    std::string              name;
    std::vector<std::string> roles;

    bool operator==(const codec_owner &) const = default;
};

struct nullable_typed_report {
    codec_report_state                                   state{};
    std::optional<std::string>                           note;
    typed_array<std::int32_t>                            samples;
    std::variant<typed_array<double>, std::string>       result;
    std::unique_ptr<codec_owner>                         owner;
    std::shared_ptr<codec_owner>                         observer;
    std::map<std::string, std::vector<std::vector<int>>> groups;
};

struct nullable_typed_report_value {
    codec_report_state                                   state{};
    std::optional<std::string>                           note;
    std::vector<std::int32_t>                            samples;
    std::variant<std::vector<double>, std::string>       result;
    std::optional<codec_owner>                           owner;
    std::optional<codec_owner>                           observer;
    std::map<std::string, std::vector<std::vector<int>>> groups;

    bool operator==(const nullable_typed_report_value &) const = default;
};

nullable_typed_report_value semantic_value(const nullable_typed_report &report) {
    std::variant<std::vector<double>, std::string> result;
    if (report.result.index() == 0) {
        result = std::get<0>(report.result).values();
    } else {
        result = std::get<1>(report.result);
    }

    return {
        report.state,
        report.note,
        report.samples.values(),
        std::move(result),
        report.owner ? std::optional<codec_owner>{*report.owner} : std::nullopt,
        report.observer ? std::optional<codec_owner>{*report.observer} : std::nullopt,
        report.groups,
    };
}

struct graph_codec_node {
    std::uint64_t              id{};
    std::string                name;
    typed_array<std::int16_t>  samples;
    std::optional<std::string> note;
};

struct graph_codec_report {
    codec_report_state                                           state{};
    std::shared_ptr<graph_codec_node>                            primary;
    std::vector<std::shared_ptr<graph_codec_node>>               mirrors;
    std::variant<std::shared_ptr<graph_codec_node>, std::string> selected;
    std::unique_ptr<codec_owner>                                 fallback;
    std::map<std::string, std::vector<int>>                      history;
};

struct graph_codec_cycle_node {
    typed_array<std::int16_t>               samples;
    std::shared_ptr<graph_codec_cycle_node> next;
};

#ifdef CBOR_TAGS_TEST_HAS_STD_EXPECTED

struct codec_error {
    std::uint32_t code{};
    std::string   message;

    bool operator==(const codec_error &) const = default;
};

struct expected_typed_report {
    codec_report_state                                     state{};
    std::expected<typed_array<std::int32_t>, codec_error>  samples;
    std::expected<std::optional<std::string>, codec_error> note;
    std::variant<std::string, bool>                        result;
    std::map<std::string, std::vector<int>>                history;
};

struct expected_typed_report_value {
    codec_report_state                                     state{};
    std::expected<std::vector<std::int32_t>, codec_error>  samples;
    std::expected<std::optional<std::string>, codec_error> note;
    std::variant<std::string, bool>                        result;
    std::map<std::string, std::vector<int>>                history;

    bool operator==(const expected_typed_report_value &) const = default;
};

expected_typed_report_value semantic_value(const expected_typed_report &report) {
    std::expected<std::vector<std::int32_t>, codec_error> samples = std::unexpected<codec_error>{codec_error{}};
    if (report.samples) {
        samples = report.samples->values();
    } else {
        samples = std::unexpected<codec_error>{report.samples.error()};
    }

    return {report.state, std::move(samples), report.note, report.result, report.history};
}

#endif

} // namespace

TEST_CASE("custom codec roundtrips realistic aggregate composition") {
    auto check_roundtrip = [](const compact_codec_report &input) {
        compact_codec_report output;
        auto                 encoded = as_custom_codec_1(static_tag<61000>{}, input);
        auto                 decoded = as_custom_codec_1(static_tag<61000>{}, output);

        test_support::roundtrip_into<custom_codec_1>(encoded, decoded);
        CHECK(output == input);
    };

    const std::vector<compact_codec_event> events{
        {"temperature", std::int64_t{-12}, std::string{"C"}, {{1, 2, 3}, {4}}},
        {"mode", std::string{"maintenance"}, std::nullopt, {{5, 6}}},
        {"interlock", true, std::nullopt, {}},
    };
    const std::map<std::string, std::vector<int>> history{{"recent", {1, 2, 3}}, {"older", {4}}};

    SUBCASE("text result and engaged optional") {
        check_roundtrip({codec_report_state::active, std::string{"calibrated"}, events, std::string{"ready"}, history});
    }
    SUBCASE("array result and disengaged optional") {
        check_roundtrip({codec_report_state::idle, std::nullopt, events, std::vector<int>{7, 8, 9}, history});
    }
    SUBCASE("simple result") { check_roundtrip({codec_report_state::degraded, std::string{"fault"}, events, false, history}); }
}

TEST_CASE("typed array and nullable pointer codecs roundtrip aggregate composition") {
    auto check_roundtrip = [](const nullable_typed_report &input) {
        const auto output = test_support::roundtrip<typed_array_codec, nullable_ptr_codec>(input);
        CHECK(semantic_value(output) == semantic_value(input));
    };

    SUBCASE("typed result with engaged pointers and optional") {
        nullable_typed_report input{
            codec_report_state::active,
            std::string{"calibrated"},
            typed_array<std::int32_t>{{1, -2, 3}},
            typed_array<double>{{1.5, -2.25}},
            std::make_unique<codec_owner>(codec_owner{1, "primary", {"write", "read"}}),
            std::make_shared<codec_owner>(codec_owner{2, "observer", {"read"}}),
            {{"primary", {{1, 2}, {3}}}, {"secondary", {{4, 5, 6}}}},
        };

        check_roundtrip(input);
    }

    SUBCASE("text result with null pointers and optional") {
        nullable_typed_report input{
            codec_report_state::idle, std::nullopt, typed_array<std::int32_t>{0}, std::string{"offline"}, nullptr, nullptr, {{"empty", {}}},
        };

        check_roundtrip(input);
    }
}

TEST_CASE("typed array nullable and shared graph codecs preserve aggregate identity") {
    auto check_roundtrip = [](graph_codec_report &input, const std::shared_ptr<codec_owner> &nullable_root, bool selected_is_pointer) {
        std::vector<std::byte>      buffer;
        auto                        enc = make_encoder<typed_array_codec, nullable_ptr_codec, shared_graph_codec>(buffer);
        shared_graph_encode_session encode_session;
        REQUIRE(enc(as_shared_graph(encode_session, input)));
        REQUIRE(enc(nullable_root));

        graph_codec_report           output;
        std::shared_ptr<codec_owner> decoded_nullable_root;
        auto                         dec = make_decoder<typed_array_codec, nullable_ptr_codec, shared_graph_codec>(buffer);
        shared_graph_decode_session  decode_session;
        REQUIRE(dec(as_shared_graph(decode_session, output)));
        REQUIRE(dec(decoded_nullable_root));
        REQUIRE(dec.tell() == buffer.end());

        CHECK_EQ(output.state, input.state);
        REQUIRE(static_cast<bool>(output.primary));
        CHECK_EQ(output.primary->id, input.primary->id);
        CHECK_EQ(output.primary->name, input.primary->name);
        CHECK_EQ(output.primary->samples.values(), input.primary->samples.values());
        CHECK_EQ(output.primary->note, input.primary->note);
        REQUIRE_EQ(output.mirrors.size(), input.mirrors.size());
        REQUIRE_EQ(output.mirrors.size(), 5U);
        const auto primary_address = static_cast<const void *>(output.primary.get());
        CHECK(static_cast<const void *>(output.mirrors[0].get()) == primary_address);
        REQUIRE(static_cast<bool>(output.mirrors[1]));
        CHECK_FALSE(output.mirrors[2]);
        CHECK(static_cast<const void *>(output.mirrors[3].get()) == primary_address);
        CHECK(static_cast<const void *>(output.mirrors[4].get()) == static_cast<const void *>(output.mirrors[1].get()));
        CHECK_EQ(output.mirrors[1]->id, input.mirrors[1]->id);
        CHECK_EQ(output.mirrors[1]->name, input.mirrors[1]->name);
        CHECK_EQ(output.mirrors[1]->samples.values(), input.mirrors[1]->samples.values());
        CHECK_EQ(output.mirrors[1]->note, input.mirrors[1]->note);
        CHECK_EQ(output.history, input.history);

        if (selected_is_pointer) {
            REQUIRE(std::holds_alternative<std::shared_ptr<graph_codec_node>>(output.selected));
            CHECK(static_cast<const void *>(std::get<std::shared_ptr<graph_codec_node>>(output.selected).get()) == primary_address);
        } else {
            REQUIRE(std::holds_alternative<std::string>(output.selected));
            CHECK_EQ(std::get<std::string>(output.selected), std::get<std::string>(input.selected));
        }

        CHECK_EQ(static_cast<bool>(output.fallback), static_cast<bool>(input.fallback));
        if (input.fallback) {
            REQUIRE(static_cast<bool>(output.fallback));
            CHECK(*output.fallback == *input.fallback);
        }

        CHECK_EQ(static_cast<bool>(decoded_nullable_root), static_cast<bool>(nullable_root));
        if (nullable_root) {
            REQUIRE(static_cast<bool>(decoded_nullable_root));
            CHECK(*decoded_nullable_root == *nullable_root);
        }
    };

    auto primary   = std::make_shared<graph_codec_node>(graph_codec_node{1, "primary", typed_array<std::int16_t>{{1, 2, 3}}, "live"});
    auto secondary = std::make_shared<graph_codec_node>(graph_codec_node{2, "secondary", typed_array<std::int16_t>{{4, 5}}, std::nullopt});

    SUBCASE("shared pointer variant and non-null nullable pointer") {
        graph_codec_report input{codec_report_state::active,
                                 primary,
                                 {primary, secondary, nullptr, primary, secondary},
                                 primary,
                                 std::make_unique<codec_owner>(codec_owner{3, "fallback", {"read"}}),
                                 {{"recent", {1, 2, 3}}}};
        check_roundtrip(input, std::make_shared<codec_owner>(codec_owner{4, "outside-graph", {"audit"}}), true);
    }

    SUBCASE("text variant and null nullable pointer") {
        graph_codec_report input{codec_report_state::degraded, primary, {primary, secondary, nullptr, primary, secondary},
                                 std::string{"manual"},        nullptr, {{"recent", {4, 5}}}};
        check_roundtrip(input, nullptr, false);
    }
}

TEST_CASE("composed shared graph stack rejects cycles and requires nullable fallback") {
    SUBCASE("typed graph cycle") {
        auto cycle     = std::make_shared<graph_codec_cycle_node>();
        cycle->samples = typed_array<std::int16_t>{{1, 2, 3}};
        cycle->next    = cycle;

        std::vector<std::byte>      buffer;
        auto                        enc = make_encoder<typed_array_codec, nullable_ptr_codec, shared_graph_codec>(buffer);
        shared_graph_encode_session session;
        const auto                  result = enc(as_shared_graph(session, cycle));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
    }

    SUBCASE("shared pointer outside graph scope") {
        const auto             value = std::make_shared<codec_owner>(codec_owner{5, "outside", {"read"}});
        std::vector<std::byte> buffer;
        auto                   enc    = make_encoder<typed_array_codec, shared_graph_codec>(buffer);
        const auto             result = enc(value);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK(buffer.empty());
    }
}

#ifdef CBOR_TAGS_TEST_HAS_STD_EXPECTED

TEST_CASE("typed array and std expected codecs roundtrip success and error aggregate states") {
    auto check_roundtrip = [](const expected_typed_report &input) {
        const auto output = test_support::roundtrip<typed_array_codec, std_expected_codec>(input);
        CHECK(semantic_value(output) == semantic_value(input));
    };

    SUBCASE("value alternatives") {
        expected_typed_report input{
            codec_report_state::active,
            typed_array<std::int32_t>{{1, -2, 3}},
            std::optional<std::string>{"calibrated"},
            std::string{"ready"},
            {{"recent", {1, 2, 3}}},
        };

        check_roundtrip(input);
    }

    SUBCASE("error alternatives") {
        expected_typed_report input{
            codec_report_state::degraded,
            std::unexpected<codec_error>{codec_error{17, "samples unavailable"}},
            std::unexpected<codec_error>{codec_error{18, "note unavailable"}},
            false,
            {{"failed", {17, 18}}},
        };

        check_roundtrip(input);
    }
}

#endif
