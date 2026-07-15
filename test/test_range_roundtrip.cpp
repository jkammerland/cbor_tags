#include "cbor_roundtrip.h"
#include "test_util.h"

#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_lazy_tags.h>
#include <cbor_tags/cbor_ranges.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

using namespace cbor::tags;
namespace test_support = cbor::tags::test;

namespace {

struct roundtrip_member_pair {
    int first;
    int second;
};

enum class ranged_report_state : std::uint8_t { idle, active };

struct ranged_sample {
    static constexpr std::uint64_t cbor_tag = 60110;

    std::string                                   name;
    std::variant<std::int64_t, std::string, bool> value;
    std::optional<std::string>                    unit;
    std::vector<std::vector<int>>                 windows;

    bool operator==(const ranged_sample &) const = default;
};

template <typename Samples, typename Metrics, typename Payload, typename Label> struct ranged_report_input {
    ranged_report_state        state{};
    std::optional<std::string> note;
    Samples                    samples;
    Metrics                    metrics;
    Payload                    payload;
    Label                      label;
};

template <typename Samples, typename Metrics, typename Payload, typename Label>
ranged_report_input(ranged_report_state, std::optional<std::string>, Samples, Metrics, Payload, Label)
    -> ranged_report_input<Samples, Metrics, Payload, Label>;

struct ranged_report_output {
    ranged_report_state                     state{};
    std::optional<std::string>              note;
    std::vector<ranged_sample>              samples;
    std::map<std::string, std::vector<int>> metrics;
    std::vector<std::byte>                  payload;
    std::string                             label;

    bool operator==(const ranged_report_output &) const = default;
};

template <typename Encoded, typename Decoded> std::vector<std::byte> roundtrip_through_vector(Encoded &&encoded, Decoded &decoded) {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(std::forward<Encoded>(encoded)));

    auto dec = make_decoder(buffer);
    REQUIRE(dec(decoded));
    return buffer;
}

template <typename Encoded, typename Decoded> std::vector<std::byte> roundtrip_through_deque(Encoded &&encoded, Decoded &decoded) {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(std::forward<Encoded>(encoded)));

    std::deque<std::byte> input(buffer.begin(), buffer.end());
    auto                  dec = make_decoder(input);
    REQUIRE(dec(decoded));
    return buffer;
}

} // namespace

TEST_CASE("explicit range wrappers roundtrip realistic aggregate composition") {
    std::vector<ranged_sample> samples{
        {"temperature", std::int64_t{-12}, std::string{"C"}, {{1, 2, 3}, {4}}},
        {"mode", std::string{"maintenance"}, std::nullopt, {{5, 6}}},
        {"interlock", true, std::nullopt, {}},
    };
    std::vector<std::pair<std::string, std::vector<int>>> metrics{
        {"recent", {1, 2, 3}},
        {"older", {4}},
    };
    std::vector<std::byte> payload{std::byte{0x00}, std::byte{0x7f}, std::byte{0xff}};
    std::string            label{"line-controller"};

    auto input = ranged_report_input{
        ranged_report_state::active, std::optional<std::string>{"calibrated"},
        as_array_range(samples),     as_map_range(metrics),
        as_bstr_range(payload),      as_tstr_range(label),
    };
    ranged_report_output output;
    ranged_report_output expected{
        ranged_report_state::active, std::string{"calibrated"}, samples, {{"older", {4}}, {"recent", {1, 2, 3}}}, payload, label};

    test_support::roundtrip_into(input, output);
    CHECK(output == expected);
}

TEST_CASE("explicit array range wrappers roundtrip sized and unsized views") {
    {
        auto             sized = std::views::iota(1, 4);
        std::vector<int> decoded;

        roundtrip_through_vector(as_array_range(sized), decoded);
        CHECK_EQ(decoded, (std::vector<int>{1, 2, 3}));
    }

    {
        auto             evens = std::views::iota(0, 6) | std::views::filter([](int value) { return value % 2 == 0; });
        std::vector<int> decoded;

        roundtrip_through_vector(as_array_range(evens), decoded);
        CHECK_EQ(decoded, (std::vector<int>{0, 2, 4}));
    }
}

TEST_CASE("explicit array range wrappers roundtrip owning and proxy ranges") {
    {
        std::vector<int> decoded;

        roundtrip_through_vector(as_array_range(std::vector<int>{4, 5}), decoded);
        CHECK_EQ(decoded, (std::vector<int>{4, 5}));
    }

    {
        std::vector<bool> flags{true, false, true};
        std::vector<bool> decoded;

        roundtrip_through_vector(as_array_range(flags), decoded);
        CHECK(decoded == flags);
    }
}

TEST_CASE("explicit map range wrappers roundtrip pair-like views") {
    {
        auto               pairs = std::views::iota(1, 4) | std::views::transform([](int value) { return std::pair{value, value + 10}; });
        std::map<int, int> decoded;

        roundtrip_through_vector(as_map_range(pairs), decoded);
        CHECK(decoded == (std::map<int, int>{{1, 11}, {2, 12}, {3, 13}}));
    }

    {
        auto               odd_pairs = std::views::iota(0, 5) | std::views::filter([](int value) { return value % 2 == 1; }) |
                                       std::views::transform([](int value) { return std::pair{value, value * 10}; });
        std::map<int, int> decoded;

        roundtrip_through_vector(as_map_range(odd_pairs), decoded);
        CHECK(decoded == (std::map<int, int>{{1, 10}, {3, 30}}));
    }
}

TEST_CASE("explicit map range wrappers roundtrip tuple, member, and nested values") {
    {
        auto               tuple_pairs = std::array{std::tuple{1, 2}, std::tuple{3, 4}};
        std::map<int, int> decoded;

        roundtrip_through_vector(as_map_range(tuple_pairs), decoded);
        CHECK(decoded == (std::map<int, int>{{1, 2}, {3, 4}}));
    }

    {
        auto               member_pairs = std::array{roundtrip_member_pair{1, 2}, roundtrip_member_pair{3, 4}};
        std::map<int, int> decoded;

        roundtrip_through_vector(as_map_range(member_pairs), decoded);
        CHECK(decoded == (std::map<int, int>{{1, 2}, {3, 4}}));
    }

    {
        std::vector<int> nested_values{1, 2};
        auto nested_entries = std::array{std::pair{1, as_array_range(nested_values)}, std::pair{2, as_array_range(nested_values)}};
        std::map<int, std::vector<int>> decoded;

        roundtrip_through_vector(as_map_range(nested_entries), decoded);
        CHECK(decoded == (std::map<int, std::vector<int>>{{1, {1, 2}}, {2, {1, 2}}}));
    }
}

TEST_CASE("explicit byte string range wrappers roundtrip byte-like views") {
    {
        auto bytes = std::views::iota(1, 4) | std::views::transform([](int value) { return static_cast<std::uint8_t>(value); });
        std::vector<std::byte> decoded;

        roundtrip_through_vector(as_bstr_range(bytes), decoded);
        CHECK_EQ(decoded, (std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}}));
    }

    {
        std::array<std::byte, 5> source{std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
        auto                     bytes = source | std::views::filter([](std::byte) { return true; });
        std::vector<std::byte>   decoded;

        roundtrip_through_vector(as_bstr_range(bytes, 2), decoded);
        CHECK_EQ(decoded, (std::vector<std::byte>{std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}}));
    }
}

TEST_CASE("explicit text string range wrappers roundtrip char views") {
    {
        std::string text{"hello"};
        auto        chars = text | std::views::transform([](char value) { return value; });
        std::string decoded;

        roundtrip_through_vector(as_tstr_range(chars), decoded);
        CHECK_EQ(decoded, "hello");
    }

    {
        std::string text{"hello"};
        auto        chars = text | std::views::filter([](char) { return true; });
        std::string decoded;

        roundtrip_through_vector(as_tstr_range(chars, 2), decoded);
        CHECK_EQ(decoded, "hello");
    }
}

TEST_CASE("explicit range wrappers pin representative CBOR wire forms") {
    SUBCASE("definite and indefinite arrays") {
        auto             sized = std::views::iota(1, 4);
        std::vector<int> decoded;
        CHECK_EQ(to_hex(roundtrip_through_vector(as_array_range(sized), decoded)), "83010203");

        auto filtered = sized | std::views::filter([](int value) { return value != 2; });
        decoded.clear();
        CHECK_EQ(to_hex(roundtrip_through_vector(as_array_range(filtered), decoded)), "9f0103ff");
    }

    SUBCASE("definite and indefinite maps") {
        std::array         pairs{std::pair{1, 2}, std::pair{3, 4}};
        std::map<int, int> decoded;
        CHECK_EQ(to_hex(roundtrip_through_vector(as_map_range(pairs), decoded)), "a201020304");

        auto filtered = pairs | std::views::filter([](const auto &entry) { return entry.first == 3; });
        decoded.clear();
        CHECK_EQ(to_hex(roundtrip_through_vector(as_map_range(filtered), decoded)), "bf0304ff");
    }

    SUBCASE("definite and chunked strings") {
        std::array<std::byte, 3> bytes{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
        std::vector<std::byte>   decoded_bytes;
        CHECK_EQ(to_hex(roundtrip_through_vector(as_bstr_range(bytes), decoded_bytes)), "43010203");

        std::string decoded_text;
        std::string text{"hello"};
        auto        filtered = text | std::views::filter([](char) { return true; });
        CHECK_EQ(to_hex(roundtrip_through_vector(as_tstr_range(filtered, 2), decoded_text)), "7f626865626c6c616fff");
    }
}

TEST_CASE("explicit range wrapper output decodes from non-contiguous input buffers") {
    auto             values = std::views::iota(0, 6) | std::views::filter([](int value) { return value % 2 == 0; });
    std::vector<int> decoded;

    roundtrip_through_deque(as_array_range(values), decoded);
    CHECK_EQ(decoded, (std::vector<int>{0, 2, 4}));
}

TEST_CASE("lazy tag payload decoders roundtrip explicit range payloads") {
    std::vector<int> values{3, 4, 5};
    auto             pairs = values | std::views::transform([](int value) { return std::pair{value, value * 2}; });

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(
        enc(as_array{2}, make_tag_pair(static_tag<77>{}, as_array_range(values)), make_tag_pair(static_tag<88>{}, as_map_range(pairs))));

    {
        auto view = find_tags<77>(buffer);
        auto it   = view.begin();

        REQUIRE(it != view.end());
        CHECK_EQ(it->tag(), 77);
        CHECK_EQ(to_hex(it->payload_range()), "83030405");

        std::vector<int> decoded;
        REQUIRE(it->decode(decoded));
        CHECK_EQ(decoded, values);

        ++it;
        CHECK(it == view.end());
        CHECK_EQ(view.status(), status_code::success);
    }

    {
        auto view = find_tags<88>(buffer);
        auto it   = view.begin();

        REQUIRE(it != view.end());
        CHECK_EQ(it->tag(), 88);

        std::map<int, int> decoded;
        REQUIRE(it->decode(decoded));
        CHECK(decoded == (std::map<int, int>{{3, 6}, {4, 8}, {5, 10}}));

        ++it;
        CHECK(it == view.end());
        CHECK_EQ(view.status(), status_code::success);
    }
}
