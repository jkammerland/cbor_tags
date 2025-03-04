#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "test_util.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <fmt/format.h>
#include <list>
#include <string_view>

using namespace cbor::tags;
using namespace cbor::tags::literals;
using namespace std::string_view_literals;

TEST_CASE("Test view contiguous, tstr") {
    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);

    enc(140_tag, wrap_as_array{1, "hello"sv});
    fmt::print("data: {}\n", to_hex(data));

    auto             dec = make_decoder(data);
    int              a;
    std::string_view b;

    auto result = dec(140_tag, wrap_as_array{a, b});
    REQUIRE(result);

    CHECK_EQ(a, 1);
    CHECK_EQ(b, "hello");

    // Check that view is in original data
    auto m1 = static_cast<const void *>(b.data());
    auto m2 = static_cast<const void *>(&data[5]);
    CHECK_EQ(m1, m2);
}

TEST_CASE("Test view contiguous, bstr") {
    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);

    enc(140_tag,
        wrap_as_array{1, std::array<std::byte, 5>{static_cast<std::byte>('h'), static_cast<std::byte>('e'), static_cast<std::byte>('l'),
                                                  static_cast<std::byte>('l'), static_cast<std::byte>('o')}});
    fmt::print("data: {}\n", to_hex(data));

    auto                       dec = make_decoder(data);
    int                        a;
    std::span<const std::byte> b;
    auto                       result = dec(140_tag, wrap_as_array{a, b});
    REQUIRE(result);
    CHECK_EQ(a, 1);
    CHECK(b.size() == 5);
    CHECK_EQ(b[0], static_cast<std::byte>('h'));
    CHECK_EQ(b[1], static_cast<std::byte>('e'));
    CHECK_EQ(b[2], static_cast<std::byte>('l'));
    CHECK_EQ(b[3], static_cast<std::byte>('l'));
    CHECK_EQ(b[4], static_cast<std::byte>('o'));

    // Check that the view is still in the original memory location.
    auto m1 = static_cast<const std::byte *>(&data[5]);
    auto m2 = static_cast<const std::byte *>(b.data());
    CHECK_EQ(m1, m2);
}

TEST_CASE("Test view non contiguous data tstr") {
    auto        data = std::deque<uint8_t>{};
    auto        enc  = make_encoder(data);
    std::string str{"hello"};
    REQUIRE(enc(str));

    auto dec    = make_decoder(data);
    auto view   = decltype(dec)::tstr_view_t{};
    auto result = dec(view);
    REQUIRE(result);
    CHECK(std::ranges::equal(view, str));

    // Check that view is of original memory
    data.back() = 'x';
    CHECK_EQ(std::string(view), "hellx");
}

TEST_CASE("Test view non contiguous data bstr") {
    auto                   data = std::deque<char>{};
    auto                   enc  = make_encoder(data);
    std::vector<std::byte> vec{std::byte(0x01), std::byte(0x02), std::byte(0x03)};
    REQUIRE(enc(vec));

    auto dec    = make_decoder(data);
    auto view   = decltype(dec)::bstr_view_t{};
    auto result = dec(view);
    REQUIRE(result);

    CHECK(std::equal(view.begin(), view.end(), vec.begin()));
}

TEST_CASE_TEMPLATE("Test big data chunk view", T, std::deque<char>, std::list<uint8_t>) {
    auto                   data = T{};
    auto                   enc  = make_encoder(data);
    std::vector<std::byte> vec;

    std::ranges::generate_n(std::back_inserter(vec), 10000, []() { return static_cast<std::byte>(std::rand() % 256); });
    REQUIRE(enc(vec));

    auto dec    = make_decoder(data);
    auto view   = typename decltype(dec)::bstr_view_t{};
    auto result = dec(view);
    REQUIRE(result);

    REQUIRE(std::equal(view.begin(), view.end(), vec.begin()));

    data.back() = 0xff;
    CHECK_EQ(view.back(), std::byte{0xff});
}