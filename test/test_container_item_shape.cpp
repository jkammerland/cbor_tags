#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cstddef>
#include <deque>
#include <doctest/doctest.h>
#include <map>
#include <span>
#include <vector>

using namespace cbor::tags;

namespace {

struct empty_item {};
struct empty_item_less {
    bool operator()(const empty_item &, const empty_item &) const { return false; }
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

} // namespace

TEST_SUITE("cbor_wire/container_item_shape") {
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
