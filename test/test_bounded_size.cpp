#include "cbor_roundtrip.h"
#include "test_util.h"

#include <algorithm>
#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_ranges.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <map>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using namespace cbor::tags;
namespace test_support = cbor::tags::test;

namespace {

using bounded_test_encoder = decltype(make_encoder(std::declval<std::vector<std::byte> &>()));

template <typename T>
concept CanEncodeBounded = requires(bounded_test_encoder &enc, const T &value) { enc.encode(value); };

template <typename T>
concept CanEncodeMutableBounded = requires(bounded_test_encoder &enc, T &value) { enc.encode(value); };

using sized_array_range = array_range<std::ranges::ref_view<std::vector<int>>>;
using sized_map_range   = map_range<std::ranges::ref_view<std::vector<std::pair<int, int>>>>;
using sized_bstr_range  = bstr_range<std::ranges::ref_view<std::vector<std::byte>>>;
using sized_tstr_range  = tstr_range<std::ranges::ref_view<std::string>>;

using filtered_ints = std::ranges::filter_view<std::ranges::ref_view<std::vector<int>>, bool (*)(int)>;
using filtered_pairs =
    std::ranges::filter_view<std::ranges::ref_view<std::vector<std::pair<int, int>>>, bool (*)(const std::pair<int, int> &)>;
using filtered_bytes = std::ranges::filter_view<std::ranges::ref_view<std::vector<std::byte>>, bool (*)(std::byte)>;
using filtered_chars = std::ranges::filter_view<std::ranges::ref_view<std::string>, bool (*)(char)>;

static_assert(IsBoundedSizeWrapper<bounded_size<std::vector<int>, 0, 3>>);
static_assert(IsBoundedSizeWrapper<const bounded_size<std::vector<int>, 0, 3> &>);
static_assert(!IsBoundedSizeWrapper<std::vector<int>>);
static_assert(IsArrayRangeWrapper<sized_array_range>);
static_assert(IsMapRangeWrapper<const sized_map_range &>);
static_assert(IsBstrRangeWrapper<sized_bstr_range>);
static_assert(IsTstrRangeWrapper<const sized_tstr_range>);
static_assert(IsStringRangeWrapper<sized_bstr_range>);
static_assert(IsStringRangeWrapper<sized_tstr_range>);
static_assert(!IsStringRangeWrapper<sized_array_range>);
static_assert(IsConstView<std::string_view>);
static_assert(IsConstView<std::basic_string_view<std::byte>>);
static_assert(!IsConstView<std::span<std::byte>>);

static_assert(CanEncodeBounded<bounded_size<sized_array_range, 0, 3>>);
static_assert(CanEncodeBounded<bounded_size<sized_map_range, 0, 3>>);
static_assert(CanEncodeBounded<bounded_size<sized_bstr_range, 0, 3>>);
static_assert(CanEncodeBounded<bounded_size<sized_tstr_range, 0, 3>>);
static_assert(CanEncodeBounded<dynamic_bounded_size<sized_array_range>>);
static_assert(CanEncodeBounded<dynamic_bounded_size<sized_map_range>>);
static_assert(CanEncodeBounded<dynamic_bounded_size<sized_bstr_range>>);
static_assert(CanEncodeBounded<dynamic_bounded_size<sized_tstr_range>>);
static_assert(!CanEncodeBounded<bounded_size<array_range<filtered_ints>, 0, 3>>);
static_assert(!CanEncodeBounded<bounded_size<map_range<filtered_pairs>, 0, 3>>);
static_assert(!CanEncodeBounded<bounded_size<bstr_range<filtered_bytes>, 0, 3>>);
static_assert(!CanEncodeBounded<bounded_size<tstr_range<filtered_chars>, 0, 3>>);
static_assert(!CanEncodeBounded<dynamic_bounded_size<array_range<filtered_ints>>>);
static_assert(!CanEncodeBounded<dynamic_bounded_size<map_range<filtered_pairs>>>);
static_assert(!CanEncodeBounded<dynamic_bounded_size<bstr_range<filtered_bytes>>>);
static_assert(!CanEncodeBounded<dynamic_bounded_size<tstr_range<filtered_chars>>>);

struct bounded_document {
    bounded_size<std::string, 1, 16>                         name;
    bounded_size<std::vector<std::byte>, 0, 4>               payload;
    bounded_size<std::vector<int>, 1, 3>                     values;
    bounded_size<std::map<std::string, std::uint64_t>, 0, 2> counters;
};

enum class bounded_sensor_state : std::uint8_t { idle, active, fault };

struct bounded_sample_batch {
    static constexpr std::uint64_t cbor_tag = 60000;

    bounded_size<std::string, 1, 12>                source;
    bounded_size<std::vector<int>, 1, 4>            readings;
    std::optional<bounded_size<std::string, 0, 16>> note;
};

using bounded_report_result = std::variant<bounded_size<std::string, 1, 16>, bounded_size<std::map<std::string, int>, 1, 3>, bool>;

struct bounded_sensor_report {
    bounded_sensor_state                                        state{};
    bounded_size<std::vector<bounded_sample_batch>, 1, 3>       batches;
    bounded_report_result                                       result;
    bounded_size<std::map<std::string, std::vector<int>>, 0, 2> history;
};

struct plain_sample_batch {
    static constexpr std::uint64_t cbor_tag = 60000;

    std::string                source;
    std::vector<int>           readings;
    std::optional<std::string> note;
};

using plain_report_result = std::variant<std::string, std::map<std::string, int>, bool>;

struct plain_sensor_report {
    bounded_sensor_state                    state{};
    std::vector<plain_sample_batch>         batches;
    plain_report_result                     result;
    std::map<std::string, std::vector<int>> history;
};

struct sample_batch_value {
    std::string                source;
    std::vector<int>           readings;
    std::optional<std::string> note;

    bool operator==(const sample_batch_value &) const = default;
};

struct sensor_report_value {
    bounded_sensor_state                    state{};
    std::vector<sample_batch_value>         batches;
    plain_report_result                     result;
    std::map<std::string, std::vector<int>> history;

    bool operator==(const sensor_report_value &) const = default;
};

sensor_report_value semantic_value(const bounded_sensor_report &report) {
    std::vector<sample_batch_value> batches;
    batches.reserve(report.batches.value().size());
    for (const auto &batch : report.batches.value()) {
        batches.push_back(
            {batch.source.value(), batch.readings.value(), batch.note ? std::optional<std::string>{batch.note->value()} : std::nullopt});
    }

    plain_report_result result;
    switch (report.result.index()) {
    case 0: result = std::get<0>(report.result).value(); break;
    case 1: result = std::get<1>(report.result).value(); break;
    default: result = std::get<2>(report.result); break;
    }

    return {report.state, std::move(batches), std::move(result), report.history.value()};
}

struct dynamic_bounded_document {
    dynamic_bounded_size<std::string>                          name;
    dynamic_bounded_size<std::vector<std::byte>>               payload;
    dynamic_bounded_size<std::vector<int>>                     values;
    dynamic_bounded_size<std::map<std::string, std::uint64_t>> counters;
};

enum class dynamic_sensor_state : std::uint8_t { idle, active, fault };

struct dynamic_sample_batch {
    static constexpr std::uint64_t cbor_tag = 60001;

    std::string                                                 source;
    std::optional<std::vector<int>>                             readings;
    std::variant<std::string, std::map<std::string, int>, bool> result;

    bool operator==(const dynamic_sample_batch &) const = default;
};

struct dynamic_sensor_report {
    dynamic_sensor_state                                          state{};
    dynamic_bounded_size<std::string>                             name;
    dynamic_bounded_size<std::vector<dynamic_sample_batch>>       batches;
    dynamic_bounded_size<std::map<std::string, std::vector<int>>> history;
    std::optional<std::string>                                    note;
};

struct plain_dynamic_sensor_report {
    dynamic_sensor_state                    state{};
    std::string                             name;
    std::vector<dynamic_sample_batch>       batches;
    std::map<std::string, std::vector<int>> history;
    std::optional<std::string>              note;
};

struct dynamic_sensor_report_value {
    dynamic_sensor_state                    state{};
    std::string                             name;
    std::vector<dynamic_sample_batch>       batches;
    std::map<std::string, std::vector<int>> history;
    std::optional<std::string>              note;

    bool operator==(const dynamic_sensor_report_value &) const = default;
};

dynamic_sensor_report make_dynamic_sensor_report_output() {
    return {
        dynamic_sensor_state::idle,
        dynamic_bounded_size<std::string>{std::string{}, 1, 16},
        dynamic_bounded_size<std::vector<dynamic_sample_batch>>{std::vector<dynamic_sample_batch>{}, 1, 3},
        dynamic_bounded_size<std::map<std::string, std::vector<int>>>{std::map<std::string, std::vector<int>>{}, 0, 2},
        std::nullopt,
    };
}

dynamic_sensor_report_value semantic_value(const dynamic_sensor_report &report) {
    return {report.state, report.name.value(), report.batches.value(), report.history.value(), report.note};
}

struct bounded_extension_value {
    std::vector<int> values;
};

template <typename Self> struct bounded_extension_codec : cbor_codec_mixin_base<Self> {
    using cbor_codec_mixin_base<Self>::decode;
    using cbor_codec_mixin_base<Self>::encode;

    void encode(const bounded_extension_value &value) { static_cast<Self &>(*this).encode(value.values); }

    template <std::size_t Min, std::size_t Max> void encode(const bounded_size<bounded_extension_value, Min, Max> &bounded) {
        static_cast<Self &>(*this).encode(as_bounded_size<Min, Max>(bounded.value().values));
    }

    status_code decode(bounded_extension_value &value, major_type major, std::byte additional_info) {
        return static_cast<Self &>(*this).decode(value.values, major, additional_info);
    }

    template <std::size_t Min, std::size_t Max>
    status_code decode(bounded_size<bounded_extension_value, Min, Max> &bounded, major_type major, std::byte additional_info) {
        auto values = as_bounded_size<Min, Max>(bounded.value().values);
        return static_cast<Self &>(*this).decode(values, major, additional_info);
    }

    template <typename Value>
        requires std::same_as<std::remove_cvref_t<Value>, bounded_extension_value>
    void encode(const dynamic_bounded_size<Value> &bounded) {
        static_cast<Self &>(*this).encode(as_bounded_size(bounded.value().values, bounded.min_size(), bounded.max_size()));
    }

    template <typename Value>
        requires std::same_as<std::remove_cvref_t<Value>, bounded_extension_value>
    status_code decode(dynamic_bounded_size<Value> &bounded, major_type major, std::byte additional_info) {
        auto values = as_bounded_size(bounded.value().values, bounded.min_size(), bounded.max_size());
        return static_cast<Self &>(*this).decode(values, major, additional_info);
    }
};

struct counting_memory_resource : std::pmr::memory_resource {
    std::size_t                allocations{};
    std::pmr::memory_resource *upstream{std::pmr::new_delete_resource()};

  private:
    void *do_allocate(std::size_t bytes, std::size_t alignment) override {
        ++allocations;
        return upstream->allocate(bytes, alignment);
    }

    void do_deallocate(void *ptr, std::size_t bytes, std::size_t alignment) override { upstream->deallocate(ptr, bytes, alignment); }

    bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override { return this == &other; }
};

} // namespace

TEST_CASE("dynamic_bounded_size owns or references and rewrapping replaces bounds") {
    static_assert(!std::default_initializable<dynamic_bounded_size<std::vector<int>>>);

    std::vector<int> values{1, 2, 3};
    auto             referenced = as_bounded_size(values, 0, 3);
    static_assert(std::same_as<decltype(referenced), dynamic_bounded_size<std::vector<int> &>>);
    CHECK_EQ(&referenced.value_, &values);
    CHECK_EQ(referenced.min_size(), 0U);
    CHECK_EQ(referenced.max_size(), 3U);

    auto rebound = as_bounded_size(referenced, 1, 4);
    static_assert(std::same_as<decltype(rebound), dynamic_bounded_size<std::vector<int> &>>);
    CHECK_EQ(&rebound.value_, &values);
    CHECK_EQ(rebound.min_size(), 1U);
    CHECK_EQ(rebound.max_size(), 4U);

    auto static_rebound = as_bounded_size<2, 5>(rebound);
    static_assert(std::same_as<decltype(static_rebound), bounded_size<std::vector<int> &, 2, 5>>);
    CHECK_EQ(&static_rebound.value_, &values);

    auto dynamic_again = as_bounded_size(static_rebound, 0, 6);
    static_assert(std::same_as<decltype(dynamic_again), dynamic_bounded_size<std::vector<int> &>>);
    CHECK_EQ(&dynamic_again.value_, &values);

    auto owning = as_bounded_size(std::vector<int>{4, 5}, 0, 2);
    static_assert(std::same_as<decltype(owning), dynamic_bounded_size<std::vector<int>>>);
    auto moved = as_bounded_size(std::move(owning), 1, 3);
    static_assert(std::same_as<decltype(moved), dynamic_bounded_size<std::vector<int>>>);
    CHECK_EQ(moved.value_, (std::vector<int>{4, 5}));
    CHECK_EQ(moved.min_size(), 1U);
    CHECK_EQ(moved.max_size(), 3U);

    CHECK_THROWS_AS((void)as_bounded_size(values, 4, 3), std::invalid_argument);
}

TEST_CASE("dynamic_bounded_size roundtrips a preconfigured aggregate") {
    dynamic_bounded_document input{
        dynamic_bounded_size<std::string>{std::string{"sensor"}, 1, 16},
        dynamic_bounded_size<std::vector<std::byte>>{std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}}, 0, 4},
        dynamic_bounded_size<std::vector<int>>{std::vector<int>{1, 2, 3}, 1, 3},
        dynamic_bounded_size<std::map<std::string, std::uint64_t>>{std::map<std::string, std::uint64_t>{{"ok", 1}}, 0, 2},
    };

    dynamic_bounded_document output{
        dynamic_bounded_size<std::string>{std::string{}, 1, 16},
        dynamic_bounded_size<std::vector<std::byte>>{std::vector<std::byte>{}, 0, 4},
        dynamic_bounded_size<std::vector<int>>{std::vector<int>{}, 1, 3},
        dynamic_bounded_size<std::map<std::string, std::uint64_t>>{std::map<std::string, std::uint64_t>{}, 0, 2},
    };
    test_support::roundtrip_into(input, output);

    CHECK_EQ(output.name.value(), input.name.value());
    CHECK_EQ(output.payload.value(), input.payload.value());
    CHECK_EQ(output.values.value(), input.values.value());
    CHECK_EQ(output.counters.value(), input.counters.value());
}

TEST_CASE("dynamic_bounded_size roundtrips nested preconfigured aggregate composition") {
    auto check_roundtrip = [](const dynamic_sensor_report &input) {
        auto output = make_dynamic_sensor_report_output();
        test_support::roundtrip_into(input, output);

        CHECK(semantic_value(output) == semantic_value(input));
        CHECK_EQ(output.name.min_size(), 1U);
        CHECK_EQ(output.name.max_size(), 16U);
        CHECK_EQ(output.batches.min_size(), 1U);
        CHECK_EQ(output.batches.max_size(), 3U);
        CHECK_EQ(output.history.min_size(), 0U);
        CHECK_EQ(output.history.max_size(), 2U);
    };

    SUBCASE("map alternative and engaged optionals") {
        dynamic_sensor_report input{
            dynamic_sensor_state::active,
            dynamic_bounded_size<std::string>{std::string{"sensor-a"}, 1, 16},
            dynamic_bounded_size<std::vector<dynamic_sample_batch>>{
                std::vector<dynamic_sample_batch>{
                    {"front", std::vector<int>{1, 2, 3, 4, 5, 6}, std::map<std::string, int>{{"accepted", 6}}},
                    {"rear", std::nullopt, std::string{"offline"}},
                },
                1, 3},
            dynamic_bounded_size<std::map<std::string, std::vector<int>>>{
                std::map<std::string, std::vector<int>>{{"recent", {1, 2, 3, 4, 5}}, {"older", {6}}}, 0, 2},
            std::string{"calibrated"},
        };

        check_roundtrip(input);
    }

    SUBCASE("text and bool alternatives at outer bounds") {
        dynamic_sensor_report input{
            dynamic_sensor_state::fault,
            dynamic_bounded_size<std::string>{std::string{"x"}, 1, 16},
            dynamic_bounded_size<std::vector<dynamic_sample_batch>>{std::vector<dynamic_sample_batch>{
                                                                        {"a", std::vector<int>{}, std::string{"ok"}},
                                                                        {"b", std::nullopt, false},
                                                                        {"c", std::vector<int>{7}, true},
                                                                    },
                                                                    1, 3},
            dynamic_bounded_size<std::map<std::string, std::vector<int>>>{std::map<std::string, std::vector<int>>{}, 0, 2},
            std::nullopt,
        };

        check_roundtrip(input);
    }
}

TEST_CASE("dynamic_bounded_size rejects typed aggregate values outside configured bounds") {
    auto decode = [](const plain_dynamic_sensor_report &input, dynamic_sensor_report &output) {
        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);
        REQUIRE(enc(input));

        auto dec = make_decoder(buffer);
        return dec(output);
    };

    SUBCASE("name exceeds its configured maximum") {
        plain_dynamic_sensor_report input{
            dynamic_sensor_state::idle, std::string(17, 'x'), {{"front", std::nullopt, true}}, {}, std::nullopt,
        };
        auto output = make_dynamic_sensor_report_output();
        auto result = decode(input, output);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK(output.name.value().empty());
    }

    SUBCASE("outer record array is below its configured minimum") {
        plain_dynamic_sensor_report input{dynamic_sensor_state::idle, "sensor", {}, {}, std::nullopt};
        auto                        output = make_dynamic_sensor_report_output();
        auto                        result = decode(input, output);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK(output.batches.value().empty());
    }

    SUBCASE("outer history map exceeds its configured maximum") {
        plain_dynamic_sensor_report input{
            dynamic_sensor_state::active,         "sensor",     {{"front", std::vector<int>{1}, std::string{"ok"}}},
            {{"a", {1}}, {"b", {2}}, {"c", {3}}}, std::nullopt,
        };
        auto output = make_dynamic_sensor_report_output();
        auto result = decode(input, output);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK(output.history.value().empty());
    }
}

TEST_CASE("dynamic bounded custom codecs opt in with ordinary overloads") {
    bounded_extension_value input{{1, 2}};
    bounded_extension_value output;
    auto                    bounded_input  = as_bounded_size(input, 1, 2);
    auto                    bounded_output = as_bounded_size(output, 1, 2);

    test_support::roundtrip_into<bounded_extension_codec>(bounded_input, bounded_output);
    CHECK_EQ(output.values, input.values);

    auto invalid     = to_bytes("83010203");
    auto invalid_dec = make_decoder<bounded_extension_codec>(invalid);
    auto result      = invalid_dec(as_bounded_size(output, 1, 2));

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::size_limit_exceeded);
    CHECK_EQ(output.values, input.values);
}

TEST_CASE("dynamic bounds reject definite sizes before output allocation or mutation") {
    SUBCASE("encode") {
        std::vector<int>       values{1, 2, 3};
        std::vector<std::byte> buffer;
        auto                   enc    = make_encoder(buffer);
        auto                   result = enc(as_bounded_size(values, 0, 2));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK(buffer.empty());
    }

    SUBCASE("decode") {
        auto                   buffer = to_bytes("4101");
        std::vector<std::byte> value{std::byte{0xAA}};
        auto                   dec    = make_decoder(buffer);
        auto                   result = dec(as_bounded_size(value, 2, 4));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(value, (std::vector<std::byte>{std::byte{0xAA}}));
    }

    SUBCASE("reserve") {
        auto buffer = to_bytes("9a00010000");

        counting_memory_resource resource;
        std::pmr::vector<int>    values{&resource};
        auto                     dec    = make_decoder(buffer);
        auto                     result = dec(as_bounded_size(values, 0, 2));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(resource.allocations, 0);
        CHECK(values.empty());
    }
}

TEST_CASE("dynamic bounded indefinite decoding retains complete prefixes") {
    SUBCASE("empty array at zero maximum") {
        auto             buffer = to_bytes("9fff");
        std::vector<int> value;

        REQUIRE(make_decoder(buffer)(as_bounded_size(value, 0, 0)));
        CHECK(value.empty());
    }

    SUBCASE("array maximum") {
        auto             buffer = to_bytes("9f010203ff");
        std::vector<int> value;
        auto             result = make_decoder(buffer)(as_bounded_size(value, 0, 2));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(value, (std::vector<int>{1, 2}));
    }

    SUBCASE("array minimum") {
        auto             buffer = to_bytes("9f01ff");
        std::vector<int> value;
        auto             result = make_decoder(buffer)(as_bounded_size(value, 2, 4));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(value, (std::vector<int>{1}));
    }

    SUBCASE("byte string") {
        auto                   buffer = to_bytes("5f426162426364ff");
        std::vector<std::byte> value;
        auto                   result = make_decoder(buffer)(as_bounded_size(value, 0, 3));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(value, (std::vector<std::byte>{std::byte{'a'}, std::byte{'b'}}));
    }

    SUBCASE("text string") {
        auto        buffer = to_bytes("7f626162626364ff");
        std::string value;
        auto        result = make_decoder(buffer)(as_bounded_size(value, 0, 3));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(value, "ab");
    }

    SUBCASE("map") {
        auto                       buffer = to_bytes("bf616101616202ff");
        std::map<std::string, int> value;
        auto                       result = make_decoder(buffer)(as_bounded_size(value, 0, 1));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(value, (std::map<std::string, int>{{"a", 1}}));
    }
}

TEST_CASE("dynamic bounds preserve fixed container extent") {
    std::array<int, 2> values{1, 2};

    SUBCASE("encode") {
        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);

        REQUIRE(enc(as_bounded_size(values, 2, 2)));
        CHECK_EQ(to_hex(buffer), "820102");

        buffer.clear();
        auto result = enc(as_bounded_size(values, 0, 1));
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK(buffer.empty());
    }

    SUBCASE("decode") {
        auto               buffer = to_bytes("820304");
        std::array<int, 2> decoded{9, 9};
        auto               dec = make_decoder(buffer);

        REQUIRE(dec(as_bounded_size(decoded, 2, 2)));
        CHECK_EQ(decoded, (std::array<int, 2>{3, 4}));

        decoded           = {9, 9};
        auto rejected_dec = make_decoder(buffer);
        auto result       = rejected_dec(as_bounded_size(decoded, 3, 3));
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(decoded, (std::array<int, 2>{9, 9}));
    }
}

TEST_CASE("dynamic bounds apply only to the immediate container") {
    const std::vector<std::vector<int>> input{{1, 2, 3}};
    std::vector<std::byte>              buffer;
    auto                                enc = make_encoder(buffer);
    REQUIRE(enc(input));

    std::vector<std::vector<int>> value;
    auto                          dec = make_decoder(buffer);

    REQUIRE(dec(as_bounded_size(value, 0, 1)));
    CHECK_EQ(value, (std::vector<std::vector<int>>{{1, 2, 3}}));
}

TEST_CASE("configured dynamic bounds encode safely through variants") {
    using input_type  = std::variant<dynamic_bounded_size<std::vector<int>>, std::string>;
    using output_type = std::variant<std::vector<int>, std::string>;

    input_type  input{std::in_place_index<0>, std::vector<int>{1, 2}, 0, 2};
    output_type output;
    test_support::roundtrip_into(input, output);

    REQUIRE(std::holds_alternative<std::vector<int>>(output));
    CHECK_EQ(std::get<std::vector<int>>(output), (std::vector<int>{1, 2}));
}

TEST_CASE("bounded_size roundtrips a mixed aggregate") {
    bounded_document input{
        bounded_size<std::string, 1, 16>{std::string{"sensor"}},
        bounded_size<std::vector<std::byte>, 0, 4>{std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}}},
        bounded_size<std::vector<int>, 1, 3>{std::vector<int>{1, 2, 3}},
        bounded_size<std::map<std::string, std::uint64_t>, 0, 2>{std::map<std::string, std::uint64_t>{{"ok", 1}}},
    };

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(input));

    bounded_document output;
    auto             dec = make_decoder(buffer);
    REQUIRE(dec(output));

    CHECK_EQ(output.name.value(), input.name.value());
    CHECK_EQ(output.payload.value(), input.payload.value());
    CHECK_EQ(output.values.value(), input.values.value());
    CHECK_EQ(output.counters.value(), input.counters.value());
}

TEST_CASE("bounded_size roundtrips nested aggregate composition") {
    auto check_roundtrip = [](const bounded_sensor_report &input) {
        const auto output = test_support::roundtrip(input);
        CHECK(semantic_value(output) == semantic_value(input));
    };

    SUBCASE("map result with engaged and disengaged optionals") {
        bounded_sensor_report input{
            bounded_sensor_state::active,
            bounded_size<std::vector<bounded_sample_batch>, 1, 3>{{
                {bounded_size<std::string, 1, 12>{"front"}, bounded_size<std::vector<int>, 1, 4>{{1, 2, 3}},
                 bounded_size<std::string, 0, 16>{"stable"}},
                {bounded_size<std::string, 1, 12>{"rear"}, bounded_size<std::vector<int>, 1, 4>{{4, 5}}, std::nullopt},
            }},
            bounded_size<std::map<std::string, int>, 1, 3>{{{"accepted", 5}, {"rejected", 0}}},
            bounded_size<std::map<std::string, std::vector<int>>, 0, 2>{{{"recent", {1, 2}}, {"older", {3}}}},
        };

        check_roundtrip(input);
    }

    SUBCASE("text result at minimum container sizes") {
        bounded_sensor_report input{
            bounded_sensor_state::idle,
            bounded_size<std::vector<bounded_sample_batch>, 1, 3>{{
                {bounded_size<std::string, 1, 12>{"a"}, bounded_size<std::vector<int>, 1, 4>{{0}}, bounded_size<std::string, 0, 16>{""}},
            }},
            bounded_size<std::string, 1, 16>{"ok"},
            bounded_size<std::map<std::string, std::vector<int>>, 0, 2>{{}},
        };

        check_roundtrip(input);
    }

    SUBCASE("simple result at maximum container sizes") {
        bounded_sensor_report input{
            bounded_sensor_state::fault,
            bounded_size<std::vector<bounded_sample_batch>, 1, 3>{{
                {bounded_size<std::string, 1, 12>{"abcdefghijkl"}, bounded_size<std::vector<int>, 1, 4>{{1, 2, 3, 4}},
                 bounded_size<std::string, 0, 16>{"1234567890abcdef"}},
                {bounded_size<std::string, 1, 12>{"middle"}, bounded_size<std::vector<int>, 1, 4>{{5}}, std::nullopt},
                {bounded_size<std::string, 1, 12>{"last"}, bounded_size<std::vector<int>, 1, 4>{{6, 7}}, std::nullopt},
            }},
            true,
            bounded_size<std::map<std::string, std::vector<int>>, 0, 2>{{{"a", {}}, {"b", {8, 9}}}},
        };

        check_roundtrip(input);
    }
}

TEST_CASE("bounded_size rejects typed aggregate values outside nested bounds") {
    auto decode = [](const plain_sensor_report &input, bounded_sensor_report &output) {
        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);
        REQUIRE(enc(input));

        auto dec = make_decoder(buffer);
        return dec(output);
    };

    SUBCASE("outer array is below its minimum") {
        plain_sensor_report   input{bounded_sensor_state::idle, {}, std::string{"ok"}, {}};
        bounded_sensor_report output;
        auto                  result = decode(input, output);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK(output.batches.value().empty());
    }

    SUBCASE("nested array exceeds its maximum") {
        plain_sensor_report input{
            bounded_sensor_state::active,
            {{"front", {1, 2, 3, 4, 5}, std::nullopt}},
            std::map<std::string, int>{{"accepted", 1}},
            {},
        };
        bounded_sensor_report output;
        auto                  result = decode(input, output);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
    }

    SUBCASE("selected variant alternative exceeds its maximum") {
        plain_sensor_report input{
            bounded_sensor_state::active,
            {{"front", {1}, std::nullopt}},
            std::string(17, 'x'),
            {},
        };
        bounded_sensor_report output;
        auto                  result = decode(input, output);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK(std::holds_alternative<bounded_size<std::string, 1, 16>>(output.result));
    }
}

TEST_CASE("custom codecs opt in to bounded_size with ordinary overloads") {
    bounded_size<bounded_extension_value, 1, 2> input{bounded_extension_value{{1, 2}}};
    bounded_size<bounded_extension_value, 1, 2> output;
    test_support::roundtrip_into<bounded_extension_codec>(input, output);
    CHECK_EQ(output.value().values, input.value().values);

    auto invalid     = to_bytes("83010203");
    auto invalid_dec = make_decoder<bounded_extension_codec>(invalid);
    auto result      = invalid_dec(output);

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::size_limit_exceeded);
    CHECK_EQ(output.value().values, input.value().values);
}

TEST_CASE("bounded custom codec pins its CBOR array wire shape") {
    bounded_size<bounded_extension_value, 1, 2> input{bounded_extension_value{{1, 2}}};
    std::vector<std::byte>                      buffer;
    auto                                        enc = make_encoder<bounded_extension_codec>(buffer);

    REQUIRE(enc(input));
    CHECK_EQ(to_hex(buffer), "820102");
}

TEST_CASE("bounded_size rejects invalid definite lengths before output or decode") {
    {
        bounded_size<std::vector<int>, 0, 2> values{std::vector<int>{1, 2, 3}};
        std::vector<std::byte>               buffer;
        auto                                 enc    = make_encoder(buffer);
        auto                                 result = enc(values);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK(buffer.empty());
    }

    {
        auto                                       buffer = to_bytes("4101");
        bounded_size<std::vector<std::byte>, 2, 4> value;
        auto                                       dec    = make_decoder(buffer);
        auto                                       result = dec(value);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK(value.value().empty());
    }
}

TEST_CASE("bounded_size applies only to the immediately wrapped container") {
    const std::vector<std::vector<int>> input{{1, 2, 3}};
    std::vector<std::byte>              buffer;
    auto                                enc = make_encoder(buffer);
    REQUIRE(enc(input));

    max_size<std::vector<std::vector<int>>, 1> outer_only;
    auto                                       outer_dec = make_decoder(buffer);
    REQUIRE(outer_dec(outer_only));
    CHECK_EQ(outer_only.value(), (std::vector<std::vector<int>>{{1, 2, 3}}));

    using bounded_row = max_size<std::vector<int>, 2>;
    max_size<std::vector<bounded_row>, 1> nested_bound;
    auto                                  nested_dec = make_decoder(buffer);
    auto                                  result     = nested_dec(nested_bound);

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::size_limit_exceeded);
    CHECK(nested_bound.value().empty());
}

TEST_CASE("bounded_size validates each decoded item independently of existing destination values") {
    auto decode_definite = []<typename T>(const T &input, T &destination) {
        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);
        REQUIRE(enc(input));

        auto dec = make_decoder(buffer);
        return dec(as_bounded_size<2, 2>(destination));
    };

    SUBCASE("definite strings and containers append items within bounds") {
        std::vector<int> values{9, 8, 7};
        REQUIRE(decode_definite(std::vector<int>{1, 2}, values));
        CHECK_EQ(values, (std::vector<int>{9, 8, 7, 1, 2}));

        std::map<std::string, int> mapping{{"existing", 0}};
        REQUIRE(decode_definite(std::map<std::string, int>{{"a", 1}, {"b", 2}}, mapping));
        CHECK_EQ(mapping, (std::map<std::string, int>{{"a", 1}, {"b", 2}, {"existing", 0}}));

        std::vector<std::byte> bytes{std::byte{0x00}};
        REQUIRE(decode_definite(std::vector<std::byte>{std::byte{0xAA}, std::byte{0xBB}}, bytes));
        CHECK_EQ(bytes, (std::vector<std::byte>{std::byte{0x00}, std::byte{0xAA}, std::byte{0xBB}}));

        std::string text{"prefix:"};
        REQUIRE(decode_definite(std::string{"ok"}, text));
        CHECK_EQ(text, "prefix:ok");
    }

    SUBCASE("the existing destination does not satisfy an incoming item minimum") {
        std::vector<int>       values{9, 8, 7};
        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);
        REQUIRE(enc(std::vector<int>{1}));

        auto dec    = make_decoder(buffer);
        auto result = dec(as_bounded_size<2, 3>(values));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(values, (std::vector<int>{9, 8, 7}));
    }

    SUBCASE("indefinite item counters also exclude existing destination values") {
        auto decode_indefinite = []<typename T>(T input, T &destination) {
            std::vector<std::byte> buffer;
            auto                   enc = make_encoder(buffer);
            REQUIRE(enc(as_indefinite{input}));

            auto dec = make_decoder(buffer);
            return dec(as_bounded_size<2, 2>(destination));
        };

        std::vector<int> values{9};
        REQUIRE(decode_indefinite(std::vector<int>{1, 2}, values));
        CHECK_EQ(values, (std::vector<int>{9, 1, 2}));

        std::map<std::string, int> mapping{{"existing", 0}};
        REQUIRE(decode_indefinite(std::map<std::string, int>{{"a", 1}, {"b", 2}}, mapping));
        CHECK_EQ(mapping, (std::map<std::string, int>{{"a", 1}, {"b", 2}, {"existing", 0}}));

        std::vector<std::byte> bytes{std::byte{0x00}};
        REQUIRE(decode_indefinite(std::vector<std::byte>{std::byte{0xAA}, std::byte{0xBB}}, bytes));
        CHECK_EQ(bytes, (std::vector<std::byte>{std::byte{0x00}, std::byte{0xAA}, std::byte{0xBB}}));

        std::string text{"prefix:"};
        REQUIRE(decode_indefinite(std::string{"ok"}, text));
        CHECK_EQ(text, "prefix:ok");
    }
}

TEST_CASE("bounded_size encode validates the wrapped item independently of existing output") {
    std::vector<std::byte> buffer{std::byte{0x00}};
    auto                   enc = make_encoder(buffer);

    REQUIRE(enc(as_bounded_size<2, 2>(std::vector<int>{1, 2})));
    CHECK_EQ(buffer.front(), std::byte{0x00});

    const auto before_failure = buffer;
    auto       result         = enc(as_bounded_size<2, 2>(std::vector<int>{1, 2, 3}));

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::size_limit_exceeded);
    CHECK_EQ(buffer, before_failure);
}

TEST_CASE("definite bounded_size rejects length before reserving") {
    auto buffer = to_bytes("9a00010000");

    counting_memory_resource resource;
    std::pmr::vector<int>    values{&resource};
    auto                     dec    = make_decoder(buffer);
    auto                     result = dec(as_bounded_size<0, 2>(values));

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::size_limit_exceeded);
    CHECK_EQ(resource.allocations, 0);
    CHECK(values.empty());
}

TEST_CASE("bounded indefinite arrays retain the decoded prefix on size errors") {
    {
        auto             buffer = to_bytes("9f010203ff");
        std::vector<int> values;
        auto             dec    = make_decoder(buffer);
        auto             result = dec(as_bounded_size<0, 2>(values));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(values, (std::vector<int>{1, 2}));
    }

    {
        auto             buffer = to_bytes("9f01ff");
        std::vector<int> values;
        auto             dec    = make_decoder(buffer);
        auto             result = dec(as_bounded_size<2, 4>(values));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(values, (std::vector<int>{1}));
    }
}

TEST_CASE("bounded indefinite strings retain complete chunks on size errors") {
    {
        auto                   buffer = to_bytes("5f426162426364ff");
        std::vector<std::byte> value;
        auto                   dec    = make_decoder(buffer);
        auto                   result = dec(as_bounded_size<0, 3>(value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(value, (std::vector<std::byte>{std::byte{'a'}, std::byte{'b'}}));
    }

    {
        auto        buffer = to_bytes("7f626162626364ff");
        std::string value;
        auto        dec    = make_decoder(buffer);
        auto        result = dec(as_bounded_size<0, 3>(value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(value, "ab");
    }
}

TEST_CASE("bounded indefinite maps count completed pairs") {
    {
        auto                       buffer = to_bytes("bf616101616202ff");
        std::map<std::string, int> value;
        auto                       dec    = make_decoder(buffer);
        auto                       result = dec(as_bounded_size<0, 1>(value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(value, (std::map<std::string, int>{{"a", 1}}));
    }

    {
        auto                       buffer = to_bytes("bf616101ff");
        std::map<std::string, int> value;
        auto                       dec    = make_decoder(buffer);
        auto                       result = dec(as_bounded_size<2, 3>(value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(value, (std::map<std::string, int>{{"a", 1}}));
    }
}

TEST_CASE("bounded indefinite decoding preserves structural errors") {
    {
        auto                       buffer = to_bytes("bf6161ff");
        std::map<std::string, int> value;
        auto                       dec    = make_decoder(buffer);
        auto                       result = dec(as_bounded_size<0, 2>(value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_map_on_buffer);
        CHECK(value.empty());
    }

    {
        auto                   buffer = to_bytes("5f6161ff");
        std::vector<std::byte> value;
        auto                   dec    = make_decoder(buffer);
        auto                   result = dec(as_bounded_size<0, 2>(value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_bstr_on_buffer);
        CHECK(value.empty());
    }

    {
        auto             buffer = to_bytes("9f01");
        std::vector<int> value;
        auto             dec    = make_decoder(buffer);
        auto             result = dec(as_bounded_size<0, 2>(value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
        CHECK_EQ(value, (std::vector<int>{1}));
    }
}

TEST_CASE("bounded_size variant propagates a selected alternative size error") {
    using bounded_variant = std::variant<bounded_size<std::string, 1, 4>, int>;

    auto            buffer = to_bytes("656e616d6573");
    bounded_variant value;
    auto            dec    = make_decoder(buffer);
    auto            result = dec(value);

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::size_limit_exceeded);
    CHECK(std::holds_alternative<bounded_size<std::string, 1, 4>>(value));
}

TEST_CASE("bounded borrowed string views decode definite payloads without ownership") {
    SUBCASE("empty text view") {
        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);
        REQUIRE(enc(std::string{}));

        std::string_view value{"before"};
        auto             dec = make_decoder(buffer);
        REQUIRE(dec(as_bounded_size<0, 0>(value)));
        CHECK(value.empty());
    }

    SUBCASE("text view") {
        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);
        REQUIRE(enc(std::string{"name"}));

        std::string_view value{"before"};
        auto             dec = make_decoder(buffer);
        REQUIRE(dec(as_bounded_size<1, 4>(value)));

        CHECK_EQ(value, "name");
        CHECK(reinterpret_cast<const std::byte *>(value.data()) == buffer.data() + 1);
    }

    SUBCASE("binary view") {
        const std::vector<std::byte> payload{std::byte{0xaa}, std::byte{0xbb}};
        std::vector<std::byte>       buffer;
        auto                         enc = make_encoder(buffer);
        REQUIRE(enc(payload));

        const std::array<std::byte, 1>    sentinel{std::byte{0xcc}};
        std::basic_string_view<std::byte> value{sentinel.data(), sentinel.size()};
        auto                              dec = make_decoder(buffer);
        REQUIRE(dec(as_bounded_size<0, 2>(value)));

        CHECK_EQ(value.size(), payload.size());
        CHECK(std::ranges::equal(value, payload));
        CHECK(value.data() == buffer.data() + 1);
    }
}

TEST_CASE("bounded borrowed string views reject invalid wire shapes without rebinding") {
    SUBCASE("minimum size") {
        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);
        REQUIRE(enc(std::string{}));

        std::string_view value{"before"};
        auto             dec    = make_decoder(buffer);
        auto             result = dec(as_bounded_size<1, 4>(value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(value, "before");
    }

    SUBCASE("size limit") {
        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);
        REQUIRE(enc(std::string{"names"}));

        std::string_view value{"before"};
        auto             dec    = make_decoder(buffer);
        auto             result = dec(as_bounded_size<1, 4>(value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(value, "before");
    }

    SUBCASE("indefinite text") {
        auto             buffer = to_bytes("7f6161ff");
        std::string_view value{"before"};
        auto             dec    = make_decoder(buffer);
        auto             result = dec(as_bounded_size<0, 4>(value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_tstr_on_buffer);
        CHECK_EQ(value, "before");
    }

    SUBCASE("indefinite binary") {
        auto                              buffer = to_bytes("5f41aaff");
        const std::array<std::byte, 1>    sentinel{std::byte{0xcc}};
        std::basic_string_view<std::byte> value{sentinel.data(), sentinel.size()};
        auto                              dec    = make_decoder(buffer);
        auto                              result = dec(as_bounded_size<0, 4>(value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_bstr_on_buffer);
        CHECK(value.data() == sentinel.data());
        CHECK_EQ(value.size(), sentinel.size());
    }

    SUBCASE("non-contiguous input") {
        std::vector<std::byte> encoded;
        auto                   enc = make_encoder(encoded);
        REQUIRE(enc(std::string{"name"}));
        std::deque<std::byte> input(encoded.begin(), encoded.end());

        std::string_view value{"before"};
        auto             dec    = make_decoder(input);
        auto             result = dec(as_bounded_size<1, 4>(value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::contiguous_view_on_non_contiguous_data);
        CHECK_EQ(value, "before");
    }
}

TEST_CASE("bounded explicit range wrappers roundtrip semantic values") {
    {
        std::vector<int> values{1, 2, 3};
        std::vector<int> decoded;

        auto input = as_bounded_size<3, 3>(as_array_range(values));
        test_support::roundtrip_into(input, decoded);
        CHECK_EQ(decoded, values);
    }

    {
        std::vector<std::pair<int, int>> values{{1, 2}, {3, 4}};
        std::map<int, int>               decoded;

        auto input = as_bounded_size<2, 2>(as_map_range(values));
        test_support::roundtrip_into(input, decoded);
        CHECK_EQ(decoded, (std::map<int, int>{{1, 2}, {3, 4}}));
    }

    {
        std::vector<std::byte> values{std::byte{0x01}, std::byte{0x02}};
        std::vector<std::byte> decoded;

        auto input = as_bounded_size<2, 2>(as_bstr_range(values));
        test_support::roundtrip_into(input, decoded);
        CHECK_EQ(decoded, values);
    }

    {
        std::string values{"hello"};
        std::string decoded;

        auto input = as_bounded_size<1, 5>(as_tstr_range(values));
        test_support::roundtrip_into(input, decoded);
        CHECK_EQ(decoded, values);
    }
}

TEST_CASE("bounded explicit range wrappers reject invalid sizes before output") {
    std::vector<std::pair<int, int>> values{{1, 2}, {3, 4}};
    std::vector<std::byte>           buffer;
    auto                             enc    = make_encoder(buffer);
    auto                             result = enc(as_bounded_size<0, 1>(as_map_range(values)));

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::size_limit_exceeded);
    CHECK(buffer.empty());
}

TEST_CASE("dynamic bounded explicit range wrappers roundtrip semantic values") {
    {
        std::vector<int> values{1, 2, 3};
        std::vector<int> decoded;

        auto input = as_bounded_size(as_array_range(values), 3, 3);
        test_support::roundtrip_into(input, decoded);
        CHECK_EQ(decoded, values);
    }

    {
        std::vector<std::pair<int, int>> values{{1, 2}, {3, 4}};
        std::map<int, int>               decoded;

        auto input = as_bounded_size(as_map_range(values), 2, 2);
        test_support::roundtrip_into(input, decoded);
        CHECK_EQ(decoded, (std::map<int, int>{{1, 2}, {3, 4}}));
    }

    {
        std::vector<std::byte> values{std::byte{0x01}, std::byte{0x02}};
        std::vector<std::byte> decoded;

        auto input = as_bounded_size(as_bstr_range(values), 2, 2);
        test_support::roundtrip_into(input, decoded);
        CHECK_EQ(decoded, values);
    }

    {
        std::string values{"hello"};
        std::string decoded;

        auto input = as_bounded_size(as_tstr_range(values), 1, 5);
        test_support::roundtrip_into(input, decoded);
        CHECK_EQ(decoded, values);
    }
}

TEST_CASE("dynamic bounded explicit range wrappers reject invalid sizes before output") {
    std::vector<std::pair<int, int>> values{{1, 2}, {3, 4}};
    std::vector<std::byte>           buffer;
    auto                             enc    = make_encoder(buffer);
    auto                             result = enc(as_bounded_size(as_map_range(values), 0, 1));

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::size_limit_exceeded);
    CHECK(buffer.empty());
}

TEST_CASE("dynamic bounded indefinite wrappers preserve their wire shape") {
    std::vector<int>       values{1, 2, 3};
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);

    REQUIRE(enc(as_bounded_size(as_indefinite{values}, 0, 3)));
    CHECK_EQ(to_hex(buffer), "9f010203ff");
}

TEST_CASE("bounded explicit ranges preserve const iteration requirements") {
    std::vector<int> values{1, 2, 3};
    auto             mutable_transform = values | std::views::transform([offset = 0](int value) mutable { return value + offset; });
    static_assert(std::ranges::sized_range<decltype(mutable_transform)>);
    static_assert(!std::ranges::range<const decltype(mutable_transform)>);

    auto range   = as_array_range(std::move(mutable_transform));
    auto bounded = as_bounded_size<0, 3>(range);
    static_assert(CanEncodeMutableBounded<decltype(bounded)>);
    static_assert(!CanEncodeBounded<decltype(bounded)>);

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(bounded));
    CHECK_EQ(to_hex(buffer), "83010203");
}

TEST_CASE("bounded explicit indefinite wrappers preserve their wire-shape requirement") {
    {
        std::vector<int>       values{1, 2, 3};
        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);

        REQUIRE(enc(as_bounded_size<0, 3>(as_indefinite{values})));
        CHECK_EQ(to_hex(buffer), "9f010203ff");
    }

    {
        auto             buffer = to_bytes("83010203");
        std::vector<int> values;
        auto             dec    = make_decoder(buffer);
        auto             result = dec(as_bounded_size<0, 3>(as_indefinite{values}));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_array_on_buffer);
        CHECK(values.empty());
    }
}
