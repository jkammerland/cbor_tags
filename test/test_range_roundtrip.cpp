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
#include <ranges>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace cbor::tags;

namespace {

struct roundtrip_member_pair {
    int first;
    int second;
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

TEST_CASE("explicit array range wrappers roundtrip sized and unsized views") {
    {
        auto             sized = std::views::iota(1, 4);
        std::vector<int> decoded;

        auto buffer = roundtrip_through_vector(as_array_range(sized), decoded);

        CHECK_EQ(to_hex(buffer), "83010203");
        CHECK_EQ(decoded, (std::vector<int>{1, 2, 3}));
    }

    {
        auto             evens = std::views::iota(0, 6) | std::views::filter([](int value) { return value % 2 == 0; });
        std::vector<int> decoded;

        auto buffer = roundtrip_through_vector(as_array_range(evens), decoded);

        CHECK_EQ(to_hex(buffer), "9f000204ff");
        CHECK_EQ(decoded, (std::vector<int>{0, 2, 4}));
    }
}

TEST_CASE("explicit array range wrappers roundtrip owning and proxy ranges") {
    {
        std::vector<int> decoded;

        auto buffer = roundtrip_through_vector(as_array_range(std::vector<int>{4, 5}), decoded);

        CHECK_EQ(to_hex(buffer), "820405");
        CHECK_EQ(decoded, (std::vector<int>{4, 5}));
    }

    {
        std::vector<bool> flags{true, false, true};
        std::vector<bool> decoded;

        auto buffer = roundtrip_through_vector(as_array_range(flags), decoded);

        CHECK_EQ(to_hex(buffer), "83f5f4f5");
        CHECK(decoded == flags);
    }
}

TEST_CASE("explicit map range wrappers roundtrip pair-like views") {
    {
        auto               pairs = std::views::iota(1, 4) | std::views::transform([](int value) { return std::pair{value, value + 10}; });
        std::map<int, int> decoded;

        auto buffer = roundtrip_through_vector(as_map_range(pairs), decoded);

        CHECK_EQ(to_hex(buffer), "a3010b020c030d");
        CHECK(decoded == (std::map<int, int>{{1, 11}, {2, 12}, {3, 13}}));
    }

    {
        auto odd_pairs = std::views::iota(0, 5) | std::views::filter([](int value) { return value % 2 == 1; }) |
                         std::views::transform([](int value) { return std::pair{value, value * 10}; });
        std::map<int, int> decoded;

        auto buffer = roundtrip_through_vector(as_map_range(odd_pairs), decoded);

        CHECK_EQ(to_hex(buffer), "bf010a03181eff");
        CHECK(decoded == (std::map<int, int>{{1, 10}, {3, 30}}));
    }
}

TEST_CASE("explicit map range wrappers roundtrip tuple, member, and nested values") {
    {
        auto               tuple_pairs = std::array{std::tuple{1, 2}, std::tuple{3, 4}};
        std::map<int, int> decoded;

        auto buffer = roundtrip_through_vector(as_map_range(tuple_pairs), decoded);

        CHECK_EQ(to_hex(buffer), "a201020304");
        CHECK(decoded == (std::map<int, int>{{1, 2}, {3, 4}}));
    }

    {
        auto               member_pairs = std::array{roundtrip_member_pair{1, 2}, roundtrip_member_pair{3, 4}};
        std::map<int, int> decoded;

        auto buffer = roundtrip_through_vector(as_map_range(member_pairs), decoded);

        CHECK_EQ(to_hex(buffer), "a201020304");
        CHECK(decoded == (std::map<int, int>{{1, 2}, {3, 4}}));
    }

    {
        std::vector<int> nested_values{1, 2};
        auto nested_entries = std::array{std::pair{1, as_array_range(nested_values)}, std::pair{2, as_array_range(nested_values)}};
        std::map<int, std::vector<int>> decoded;

        auto buffer = roundtrip_through_vector(as_map_range(nested_entries), decoded);

        CHECK_EQ(to_hex(buffer), "a20182010202820102");
        CHECK(decoded == (std::map<int, std::vector<int>>{{1, {1, 2}}, {2, {1, 2}}}));
    }
}

TEST_CASE("explicit byte string range wrappers roundtrip byte-like views") {
    {
        auto bytes = std::views::iota(1, 4) | std::views::transform([](int value) { return static_cast<std::uint8_t>(value); });
        std::vector<std::byte> decoded;

        auto buffer = roundtrip_through_vector(as_bstr_range(bytes), decoded);

        CHECK_EQ(to_hex(buffer), "43010203");
        CHECK_EQ(to_hex(decoded), "010203");
    }

    {
        std::array<std::byte, 5> source{std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
        auto                     bytes = source | std::views::filter([](std::byte) { return true; });
        std::vector<std::byte>   decoded;

        auto buffer = roundtrip_through_vector(as_bstr_range(bytes, 2), decoded);

        CHECK_EQ(to_hex(buffer), "5f4200014202034104ff");
        CHECK_EQ(to_hex(decoded), "0001020304");
    }
}

TEST_CASE("explicit text string range wrappers roundtrip char views") {
    {
        std::string text{"hello"};
        auto        chars = text | std::views::transform([](char value) { return value; });
        std::string decoded;

        auto buffer = roundtrip_through_vector(as_tstr_range(chars), decoded);

        CHECK_EQ(to_hex(buffer), "6568656c6c6f");
        CHECK_EQ(decoded, "hello");
    }

    {
        std::string text{"hello"};
        auto        chars = text | std::views::filter([](char) { return true; });
        std::string decoded;

        auto buffer = roundtrip_through_vector(as_tstr_range(chars, 2), decoded);

        CHECK_EQ(to_hex(buffer), "7f626865626c6c616fff");
        CHECK_EQ(decoded, "hello");
    }
}

TEST_CASE("explicit range wrapper output decodes from non-contiguous input buffers") {
    auto             values = std::views::iota(0, 6) | std::views::filter([](int value) { return value % 2 == 0; });
    std::vector<int> decoded;

    auto buffer = roundtrip_through_deque(as_array_range(values), decoded);

    CHECK_EQ(to_hex(buffer), "9f000204ff");
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
