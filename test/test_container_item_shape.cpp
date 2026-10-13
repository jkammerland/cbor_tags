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
#include <tuple>
#include <variant>
#include <vector>

using namespace cbor::tags;

namespace {

struct empty_item {};
struct empty_item_less {
    bool operator()(const empty_item &, const empty_item &) const { return false; }
};

struct pair_item {
    unsigned first;
    unsigned second;
};
struct tagged_pair_item {
    static constexpr std::uint64_t cbor_tag = 42;
    unsigned                       first;
    unsigned                       second;
};

// This codec deliberately gives an otherwise empty aggregate one complete item.
template <typename Self> struct empty_item_codec : codec::base<Self> {
    using codec::base<Self>::encode;
    using codec::base<Self>::decode;

    void encode(const empty_item &) { static_cast<Self &>(*this).encode(nullptr); }

    status_code decode(empty_item &) {
        std::nullptr_t value{};
        return static_cast<Self &>(*this).decode(value);
    }

    status_code decode(empty_item &, major_type major, std::byte info) {
        std::nullptr_t value{};
        return static_cast<Self &>(*this).decode(value, major, info);
    }
};

template <typename T> void check_empty_wire(T &value, const std::vector<std::byte> &wire) {
    std::vector<std::byte> bytes;
    auto                   enc = make_encoder(bytes);
    REQUIRE(enc(value));
    CHECK(bytes == wire);

    auto dec = make_decoder(wire);
    REQUIRE(dec(value));
    CHECK(dec.tell() == wire.end());
}

template <typename T> void check_count_wire(const std::vector<std::byte> &wire, std::size_t count) {
    T    value;
    auto dec = make_decoder(wire);
    REQUIRE(dec(value));
    CHECK_EQ(value.size(), count);
    CHECK(dec.tell() == wire.end());
}

template <typename T> void check_compact_pair_wire(const std::vector<std::byte> &wire) {
    using compact_options = Options<default_expected>;
    static_assert(!compact_options::wrap_groups);

    std::vector<T>         value{{1, 2}};
    std::vector<std::byte> bytes;
    encoder<decltype(bytes), compact_options, cbor_header_encoder, cbor_indefinite_encoder, cbor_optional_encoder, cbor_variant_encoder>
        enc{bytes};
    REQUIRE(enc(value));
    CHECK(bytes == wire);

    std::vector<T> decoded;
    auto           dec = make_decoder_with_options<compact_options>(wire);
    REQUIRE(dec(decoded));
    REQUIRE_EQ(decoded.size(), 1U);
    CHECK_EQ(decoded.front().first, 1U);
    CHECK_EQ(decoded.front().second, 2U);
    CHECK(dec.tell() == wire.end());
}

} // namespace

TEST_SUITE("cbor_wire/container_item_shape") {
    TEST_CASE("compact multi-field groups remain available inside containers") {
        check_compact_pair_wire<pair_item>({std::byte{0x81}, std::byte{0x01}, std::byte{0x02}});
        check_compact_pair_wire<tagged_pair_item>({std::byte{0x81}, std::byte{0xD8}, std::byte{0x2A}, std::byte{0x01}, std::byte{0x02}});
    }

    TEST_CASE("only zero-input iterations require a bound") {
        // One map side may be zero-width when the other side consumes input.
        const std::vector<std::byte> map_wire{std::byte{0xA3}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
        check_count_wire<std::multimap<empty_item, unsigned, empty_item_less>>(map_wire, 3);
        check_count_wire<std::map<unsigned, empty_item>>(map_wire, 3);

        const std::vector<std::byte>                         missing_map_payload{std::byte{0xA3}};
        std::multimap<empty_item, unsigned, empty_item_less> map;
        auto                                                 dec    = make_decoder(missing_map_payload);
        const auto                                           result = dec(map);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
        CHECK(map.empty());

        // Headers and wrapped empty groups consume bytes without satisfying
        // the stronger complete-item rule removed from this PR.
        const std::vector<std::byte> array_wire{std::byte{0x83}, std::byte{0x80}, std::byte{0x80}, std::byte{0x80}};
        check_count_wire<std::deque<as_array_any>>(array_wire, 3);
        check_count_wire<std::deque<std::tuple<empty_item, empty_item>>>(
            {std::byte{0x83}, std::byte{0x82}, std::byte{0x82}, std::byte{0x82}}, 3);
    }

    TEST_CASE("optional and variant entry points consume their own headers") {
        const std::vector<std::byte>          nulls{std::byte{0x83}, std::byte{0xF6}, std::byte{0xF6}, std::byte{0xF6}};
        std::deque<std::optional<empty_item>> values;
        auto                                  dec = make_decoder(nulls);
        REQUIRE(dec(values));
        REQUIRE_EQ(values.size(), 3U);
        CHECK_FALSE(values[0].has_value());
        CHECK_FALSE(values[1].has_value());
        CHECK_FALSE(values[2].has_value());
        CHECK(dec.tell() == nulls.end());

        using variant_type = std::variant<unsigned, std::string>;
        check_count_wire<std::deque<variant_type>>({std::byte{0x82}, std::byte{0x01}, std::byte{0x61}, std::byte{0x78}}, 2);

        const std::vector<std::byte>          missing_payload{std::byte{0x83}};
        auto                                  missing_dec = make_decoder(missing_payload);
        std::deque<std::optional<empty_item>> missing;
        const auto                            result = missing_dec(missing);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
        CHECK(missing.empty());
    }

    TEST_CASE("explicit count bounds permit zero-byte entries and reject excess counts") {
        const std::vector<std::byte>               array_wire{std::byte{0x83}};
        bounded_size<std::deque<empty_item>, 0, 3> value;
        auto                                       dec = make_decoder(array_wire);
        REQUIRE(dec(value));
        CHECK_EQ(value.value().size(), 3U);
        CHECK(dec.tell() == array_wire.end());

        std::deque<empty_item> dynamic;
        auto                   bounded_dynamic = dynamic_bounded_size<decltype(dynamic) &>{dynamic, 0, 3};
        auto                   dynamic_dec     = make_decoder(array_wire);
        REQUIRE(dynamic_dec(bounded_dynamic));
        CHECK_EQ(dynamic.size(), 3U);

        const std::vector<std::byte>                                               map_wire{std::byte{0xA3}};
        bounded_size<std::multimap<empty_item, empty_item, empty_item_less>, 0, 3> map;
        auto                                                                       map_dec = make_decoder(map_wire);
        REQUIRE(map_dec(map));
        CHECK_EQ(map.value().size(), 3U);
        CHECK(map_dec.tell() == map_wire.end());

        const std::vector<std::byte>               excess{std::byte{0x84}};
        bounded_size<std::deque<empty_item>, 0, 3> rejected;
        auto                                       rejected_dec = make_decoder(excess);
        const auto                                 result       = rejected_dec(rejected);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK(rejected.value().empty());

        auto       dynamic_rejected     = dynamic_bounded_size<decltype(dynamic) &>{dynamic, 0, 2};
        auto       dynamic_rejected_dec = make_decoder(array_wire);
        const auto dynamic_result       = dynamic_rejected_dec(dynamic_rejected);
        REQUIRE_FALSE(dynamic_result);
        CHECK_EQ(dynamic_result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(dynamic.size(), 3U);
    }

    TEST_CASE("fixed nonzero targets and explicitly indefinite decoding remain available") {
        check_count_wire<std::array<empty_item, 3>>({std::byte{0x83}}, 3);

        const std::vector<std::byte> wire{std::byte{0x9F}, std::byte{0xFF}};
        std::deque<empty_item>       raw;
        auto                         value = as_indefinite{raw};
        auto                         dec   = make_decoder(wire);
        REQUIRE(dec(value));
        CHECK(raw.empty());
        CHECK(dec.tell() == wire.end());
    }

    TEST_CASE("fixed empty containers and static zero bounds have no children") {
        std::array<empty_item, 0> fixed{};
        check_empty_wire(fixed, {std::byte{0x80}});

        std::span<empty_item, 0> span{fixed};
        check_empty_wire(span, {std::byte{0x80}});

        bounded_size<std::deque<empty_item>, 0, 0> array;
        check_empty_wire(array, {std::byte{0x80}});

        bounded_size<std::map<empty_item, empty_item, empty_item_less>, 0, 0> map;
        check_empty_wire(map, {std::byte{0xA0}});

        std::deque<empty_item>                         raw_array;
        auto                                           indefinite_array = as_indefinite{raw_array};
        bounded_size<decltype(indefinite_array), 0, 0> bounded_array{indefinite_array};
        check_empty_wire(bounded_array, {std::byte{0x9F}, std::byte{0xFF}});

        std::map<empty_item, empty_item, empty_item_less> raw_map;
        auto                                              indefinite_map = as_indefinite{raw_map};
        bounded_size<decltype(indefinite_map), 0, 0>      bounded_map{indefinite_map};
        check_empty_wire(bounded_map, {std::byte{0xBF}, std::byte{0xFF}});
    }

    TEST_CASE("a static zero bound rejects actual children") {
        bounded_size<std::deque<empty_item>, 0, 0> value;
        value.value().emplace_back();

        std::vector<std::byte> bytes;
        auto                   enc    = make_encoder(bytes);
        const auto             result = enc(value);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK(bytes.empty());

        for (const auto &wire : {std::vector<std::byte>{std::byte{0x81}, std::byte{0xF6}},
                                 std::vector<std::byte>{std::byte{0x9F}, std::byte{0xF6}, std::byte{0xFF}}}) {
            bounded_size<std::deque<empty_item>, 0, 0> decoded;
            auto                                       dec    = make_decoder(wire);
            const auto                                 result = dec(decoded);
            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), status_code::size_limit_exceeded);
            CHECK(decoded.value().empty());
        }
    }

    TEST_CASE("a standalone empty aggregate retains its zero-width representation") {
        empty_item             value;
        std::vector<std::byte> bytes;
        auto                   enc = make_encoder(bytes);
        REQUIRE(enc(value));
        CHECK(bytes.empty());

        auto dec = make_decoder(bytes);
        REQUIRE(dec(value));
        CHECK(dec.tell() == bytes.cbegin());
    }

    TEST_CASE("custom item codecs compose through definite bounded and indefinite containers") {
        std::deque<empty_item> array(2);
        std::vector<std::byte> bytes;
        auto                   enc = make_encoder<empty_item_codec>(bytes);
        REQUIRE(enc(array));
        CHECK(bytes == std::vector<std::byte>{std::byte{0x82}, std::byte{0xF6}, std::byte{0xF6}});

        std::deque<empty_item> decoded;
        auto                   dec = make_decoder<empty_item_codec>(bytes);
        REQUIRE(dec(decoded));
        CHECK_EQ(decoded.size(), 2U);

        bytes.clear();
        auto indefinite = as_indefinite{array};
        REQUIRE(enc(indefinite));
        CHECK(bytes == std::vector<std::byte>{std::byte{0x9F}, std::byte{0xF6}, std::byte{0xF6}, std::byte{0xFF}});

        decoded.clear();
        auto decoded_indefinite = as_indefinite{decoded};
        auto indefinite_dec     = make_decoder<empty_item_codec>(bytes);
        REQUIRE(indefinite_dec(decoded_indefinite));
        CHECK_EQ(decoded.size(), 2U);

        bytes.clear();
        bounded_size<std::deque<empty_item>, 0, 2> bounded{array};
        REQUIRE(enc(bounded));
        CHECK(bytes == std::vector<std::byte>{std::byte{0x82}, std::byte{0xF6}, std::byte{0xF6}});

        bounded_size<std::deque<empty_item>, 0, 2> decoded_bounded;
        auto                                       bounded_dec = make_decoder<empty_item_codec>(bytes);
        REQUIRE(bounded_dec(decoded_bounded));
        CHECK_EQ(decoded_bounded.value().size(), 2U);

        bytes.clear();
        std::map<empty_item, empty_item, empty_item_less> map{{empty_item{}, empty_item{}}};
        REQUIRE(enc(map));
        CHECK(bytes == std::vector<std::byte>{std::byte{0xA1}, std::byte{0xF6}, std::byte{0xF6}});

        std::map<empty_item, empty_item, empty_item_less> decoded_map;
        auto                                              map_dec = make_decoder<empty_item_codec>(bytes);
        REQUIRE(map_dec(decoded_map));
        CHECK_EQ(decoded_map.size(), 1U);
    }
}
