#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_ranges.h>
#include <cbor_tags/cbor_raw_views.h>
#include <cstddef>
#include <deque>
#include <doctest/doctest.h>
#include <list>
#include <ranges>
#include <span>
#include <type_traits>
#include <vector>

using namespace cbor::tags;

namespace {

template <typename R>
concept CanMakeBstrRange = requires(R &&range) { cbor::tags::as_bstr_range(std::forward<R>(range)); };

static_assert(std::ranges::view<encoded_byte_view>);
static_assert(std::ranges::borrowed_range<encoded_byte_view>);
static_assert(std::ranges::common_range<encoded_byte_view>);
static_assert(std::ranges::sized_range<encoded_byte_view>);
static_assert(std::ranges::range<const encoded_byte_view>);
static_assert(IsByteLikeRange<encoded_byte_view>);
static_assert(CanMakeBstrRange<encoded_byte_view>);
static_assert(std::same_as<std::ranges::range_value_t<encoded_byte_view>, std::byte>);

using list_encoded_byte_view  = encoded_byte_view_for<std::list<std::byte>>;
using list_encoded_array_view = encoded_array_view_for<std::list<std::byte>>;

static_assert(std::ranges::view<list_encoded_byte_view>);
static_assert(std::ranges::borrowed_range<list_encoded_byte_view>);
static_assert(std::ranges::common_range<list_encoded_byte_view>);
static_assert(std::ranges::sized_range<list_encoded_byte_view>);
static_assert(std::ranges::range<const list_encoded_byte_view>);
static_assert(IsByteLikeRange<list_encoded_byte_view>);
static_assert(CanMakeBstrRange<list_encoded_byte_view>);
static_assert(IsEncodedArrayView<list_encoded_array_view>);

template <typename RawView> std::vector<std::byte> reencode(const RawView &view) {
    std::vector<std::byte> output;
    auto                   enc = make_encoder(output);
    REQUIRE(enc(view));
    return output;
}

void check_raw_item_decode_error(const char *hex, status_code expected) {
    auto bytes = to_bytes(hex);
    auto dec   = make_decoder(bytes);

    encoded_item_view item;
    auto              result = dec(item);

    CAPTURE(hex);
    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), expected);
}

} // namespace

TEST_CASE("raw encoded item views decode one item and re-encode exact bytes") {
    auto bytes = to_bytes("83010203f5");
    auto dec   = make_decoder(bytes);

    encoded_item_view item;
    bool              trailing{};

    REQUIRE(dec(item, trailing));
    CHECK_EQ(to_hex(item.bytes()), "83010203");
    CHECK_EQ(to_hex(reencode(item)), "83010203");
    CHECK(trailing);
}

TEST_CASE("raw encoded array and map views require matching top-level major type") {
    {
        auto bytes = to_bytes("83010203");
        auto dec   = make_decoder(bytes);

        encoded_array_view array;

        REQUIRE(dec(array));
        CHECK_EQ(to_hex(array.bytes()), "83010203");
        CHECK_EQ(to_hex(reencode(array)), "83010203");
    }

    {
        auto bytes = to_bytes("a201020304");
        auto dec   = make_decoder(bytes);

        encoded_map_view map;

        REQUIRE(dec(map));
        CHECK_EQ(to_hex(map.bytes()), "a201020304");
        CHECK_EQ(to_hex(reencode(map)), "a201020304");
    }

    {
        auto bytes = to_bytes("83010203");
        auto dec   = make_decoder(bytes);

        encoded_map_view map;
        auto             result = dec(map);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_map_on_buffer);
    }
}

TEST_CASE("raw encoded views preserve indefinite arrays and maps") {
    {
        auto bytes = to_bytes("9f018202039f0405ffff");
        auto dec   = make_decoder(bytes);

        encoded_array_view array;

        REQUIRE(dec(array));
        CHECK_EQ(to_hex(array.bytes()), "9f018202039f0405ffff");
        CHECK_EQ(to_hex(reencode(array)), "9f018202039f0405ffff");
    }

    {
        auto bytes = to_bytes("bf016161029f0304ffff");
        auto dec   = make_decoder(bytes);

        encoded_map_view map;

        REQUIRE(dec(map));
        CHECK_EQ(to_hex(map.bytes()), "bf016161029f0304ffff");
        CHECK_EQ(to_hex(reencode(map)), "bf016161029f0304ffff");
    }
}

TEST_CASE("raw encoded item views preserve tags and indefinite strings") {
    {
        auto bytes = to_bytes("d82a820102");
        auto dec   = make_decoder(bytes);

        encoded_item_view item;

        REQUIRE(dec(item));
        CHECK_EQ(to_hex(item.bytes()), "d82a820102");
        CHECK_EQ(to_hex(reencode(item)), "d82a820102");
    }

    {
        auto bytes = to_bytes("5f4101ff");
        auto dec   = make_decoder(bytes);

        encoded_item_view item;

        REQUIRE(dec(item));
        CHECK_EQ(to_hex(item.bytes()), "5f4101ff");
        CHECK_EQ(to_hex(reencode(item)), "5f4101ff");
    }

    {
        auto bytes = to_bytes("7f6161ff");
        auto dec   = make_decoder(bytes);

        encoded_item_view item;

        REQUIRE(dec(item));
        CHECK_EQ(to_hex(item.bytes()), "7f6161ff");
        CHECK_EQ(to_hex(reencode(item)), "7f6161ff");
    }
}

TEST_CASE("raw encoded array and map views reject tagged items with major mismatch") {
    auto bytes = to_bytes("d82a820102");

    {
        auto dec = make_decoder(bytes);

        encoded_array_view array;
        auto               result = dec(array);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_array_on_buffer);
    }

    {
        auto dec = make_decoder(bytes);

        encoded_map_view map;
        auto             result = dec(map);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_map_on_buffer);
    }
}

TEST_CASE("raw encoded views reject malformed and truncated input") {
    {
        auto bytes = to_bytes("ff");
        auto dec   = make_decoder(bytes);

        encoded_item_view item;
        auto              result = dec(item);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
    }

    {
        auto bytes = to_bytes("9f0102");
        auto dec   = make_decoder(bytes);

        encoded_array_view array;
        auto               result = dec(array);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
    }

    {
        auto bytes = to_bytes("5f6101ff");
        auto dec   = make_decoder(bytes);

        encoded_item_view item;
        auto              result = dec(item);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
    }

    {
        auto bytes = to_bytes("430102");
        auto dec   = make_decoder(bytes);

        encoded_item_view item;
        auto              result = dec(item);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
    }

    {
        auto bytes = to_bytes("bf01ff");
        auto dec   = make_decoder(bytes);

        encoded_map_view map;
        auto             result = dec(map);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
    }
}

TEST_CASE("raw encoded views reject invalid additional-info values and truncated arguments") {
    for (const auto *hex : {"1c", "1d", "1e", "1f", "5c", "9c", "bc", "dc", "fc", "fd", "fe"}) {
        check_raw_item_decode_error(hex, status_code::error);
    }

    for (const auto *hex : {"18", "19ff", "1a0000", "1b00000000000000"}) {
        check_raw_item_decode_error(hex, status_code::incomplete);
    }
}

TEST_CASE("raw encoded view bytes can be embedded as byte strings") {
    {
        auto bytes = to_bytes("820102");
        auto dec   = make_decoder(bytes);

        encoded_array_view array;
        REQUIRE(dec(array));

        std::vector<std::byte> output;
        auto                   enc = make_encoder(output);

        REQUIRE(enc(as_bstr_range(array.bytes())));
        CHECK_EQ(to_hex(output), "43820102");
    }

    {
        auto bytes = to_bytes("a10102");
        auto dec   = make_decoder(bytes);

        encoded_map_view map;
        REQUIRE(dec(map));

        std::vector<std::byte> output;
        auto                   enc = make_encoder(output);

        REQUIRE(enc(as_bstr_range(map.bytes())));
        CHECK_EQ(to_hex(output), "43a10102");
    }
}

TEST_CASE("raw encoded views expose contiguous bytes as a span") {
    auto bytes = to_bytes("820102");
    auto dec   = make_decoder(bytes);

    encoded_array_view array;

    REQUIRE(dec(array));
    auto span = array.span();

    REQUIRE(span.has_value());
    CHECK_EQ(span->data(), reinterpret_cast<const std::byte *>(bytes.data()));
    CHECK_EQ(to_hex(*span), "820102");
}

TEST_CASE("raw encoded views expose offset contiguous bytes as a span") {
    auto bytes = to_bytes("f5820102f4");
    auto dec   = make_decoder(bytes);

    bool               prefix{};
    encoded_array_view array;
    bool               suffix{};

    REQUIRE(dec(prefix, array, suffix));

    auto span = array.span();
    REQUIRE(span.has_value());
    CHECK_EQ(span->data(), reinterpret_cast<const std::byte *>(bytes.data() + 1));
    CHECK_EQ(to_hex(*span), "820102");
    CHECK(prefix);
    CHECK_FALSE(suffix);
}

TEST_CASE("raw encoded views borrow non-contiguous input without copying") {
    auto                 contiguous = to_bytes("820102");
    std::list<std::byte> bytes(contiguous.begin(), contiguous.end());
    auto                 dec = make_decoder(bytes);
    using array_view         = typename decltype(dec)::raw_encoded_array_view;
    array_view             array;
    std::vector<std::byte> output;
    auto                   enc = make_encoder(output);

    REQUIRE(dec(array));
    CHECK_FALSE(array.span().has_value());
    CHECK_EQ(to_hex(array.bytes()), "820102");

    auto it = bytes.begin();
    ++it;
    *it = std::byte{0x0A};

    CHECK_EQ(to_hex(array.bytes()), "820a02");
    REQUIRE(enc(array));
    CHECK_EQ(to_hex(output), "820a02");
}

TEST_CASE("non-contiguous raw encoded views decode iterate and re-encode without allocation") {
    auto                 contiguous = to_bytes("83010203");
    std::list<std::byte> bytes(contiguous.begin(), contiguous.end());
    auto                 dec = make_decoder(bytes);
    using array_view         = encoded_array_view_for<decltype(bytes)>;
    array_view array;

    std::vector<std::byte> observed;
    std::vector<std::byte> output;
    std::vector<std::byte> bstr_output;
    observed.reserve(contiguous.size());
    output.reserve(contiguous.size());
    bstr_output.reserve(contiguous.size() + 1U);

    bool decoded{};
    bool encoded{};
    bool bstr_encoded{};
    {
        cbor::tags::test::detail::allocation_failure_guard guard;

        decoded = dec(array).has_value();
        if (decoded) {
            for (auto byte : array.bytes()) {
                observed.push_back(static_cast<std::byte>(byte));
            }

            auto enc = make_encoder(output);
            encoded  = enc(array).has_value();

            auto bstr_enc = make_encoder(bstr_output);
            bstr_encoded  = bstr_enc(as_bstr_range(array.bytes())).has_value();
        }
    }

    REQUIRE(decoded);
    CHECK_EQ(to_hex(observed), "83010203");
    REQUIRE(encoded);
    CHECK_EQ(to_hex(output), "83010203");
    REQUIRE(bstr_encoded);
    CHECK_EQ(to_hex(bstr_output), "4483010203");
}
