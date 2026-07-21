#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "small_generator.h"
#include "test_util.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <fmt/format.h>
#include <iterator>
#include <list>
#include <optional>
#include <random>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

using namespace cbor::tags;
using namespace cbor::tags::literals;
using namespace std::string_view_literals;

namespace {

enum class view_report_state : std::uint8_t { idle, active };

struct owning_view_report {
    view_report_state                                       state{};
    std::string                                             name;
    std::vector<std::byte>                                  payload;
    std::optional<std::string>                              note;
    std::variant<std::string, std::vector<std::byte>, bool> result;
    std::array<int, 3>                                      samples{};
};

struct borrowed_view_report {
    view_report_state                                                state{};
    std::string_view                                                 name;
    std::span<const std::byte>                                       payload;
    std::optional<std::string_view>                                  note;
    std::variant<std::string_view, std::span<const std::byte>, bool> result;
    std::array<int, 3>                                               samples{};
};

} // namespace

TEST_CASE("contiguous borrowed views roundtrip aggregate composition") {
    auto check_roundtrip = [](const owning_view_report &input) {
        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);
        REQUIRE(enc(input));

        borrowed_view_report output;
        auto                 dec = make_decoder(buffer);
        REQUIRE(dec(output));
        REQUIRE(dec.tell() == buffer.end());

        auto require_borrowed_from_buffer = [&](const auto &view) {
            if (view.empty()) {
                return;
            }

            using view_type = std::remove_cvref_t<decltype(view)>;

            const auto buffer_begin = reinterpret_cast<std::uintptr_t>(buffer.data());
            const auto buffer_end   = buffer_begin + buffer.size();
            const auto view_begin   = reinterpret_cast<std::uintptr_t>(std::ranges::data(view));
            const auto view_bytes   = std::ranges::size(view) * sizeof(std::ranges::range_value_t<view_type>);

            REQUIRE_GE(view_begin, buffer_begin);
            REQUIRE_LE(view_begin, buffer_end);
            CHECK_LE(view_bytes, buffer_end - view_begin);
        };

        CHECK_EQ(output.state, input.state);
        require_borrowed_from_buffer(output.name);
        require_borrowed_from_buffer(output.payload);
        CHECK_EQ(output.name, input.name);
        CHECK(std::ranges::equal(output.payload, input.payload));
        CHECK_EQ(output.note.has_value(), input.note.has_value());
        if (input.note) {
            REQUIRE(output.note);
            require_borrowed_from_buffer(*output.note);
            CHECK_EQ(*output.note, *input.note);
        }
        REQUIRE_EQ(output.result.index(), input.result.index());
        switch (input.result.index()) {
        case 0:
            require_borrowed_from_buffer(std::get<0>(output.result));
            CHECK_EQ(std::get<0>(output.result), std::get<0>(input.result));
            break;
        case 1:
            require_borrowed_from_buffer(std::get<1>(output.result));
            CHECK(std::ranges::equal(std::get<1>(output.result), std::get<1>(input.result)));
            break;
        default: CHECK_EQ(std::get<2>(output.result), std::get<2>(input.result)); break;
        }
        CHECK_EQ(output.samples, input.samples);
    };

    SUBCASE("text variant and engaged optional") {
        check_roundtrip({view_report_state::active,
                         "line-controller",
                         {std::byte{0x00}, std::byte{0x7f}, std::byte{0xff}},
                         std::string{"calibrated"},
                         std::string{"ready"},
                         {1, 2, 3}});
    }

    SUBCASE("binary variant and disengaged optional") {
        check_roundtrip({view_report_state::idle,
                         "sensor",
                         {std::byte{0x01}, std::byte{0x02}},
                         std::nullopt,
                         std::vector<std::byte>{std::byte{0xaa}, std::byte{0xbb}},
                         {4, 5, 6}});
    }

    SUBCASE("simple variant") {
        check_roundtrip({view_report_state::active, "interlock", {std::byte{0x10}}, std::string{"armed"}, true, {7, 8, 9}});
    }
}

TEST_CASE("Test view contiguous, tstr") {
    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);

    CHECK(enc(140_tag, wrap_as_array{1, "hello"sv}));
    CBOR_TAGS_TEST_LOG("data: {}\n", to_hex(data));

    auto             dec = make_decoder(data);
    int              a;
    std::string_view b;

    auto result = dec(140_tag, wrap_as_array{a, b});
    REQUIRE(result);

    CHECK_EQ(a, 1);
    CHECK_EQ(b, "hello");

    // Check that view is in original data
    const auto *m1 = static_cast<const void *>(b.data());
    const auto *m2 = static_cast<const void *>(&data[5]);
    CHECK_EQ(m1, m2);
}

TEST_CASE("Test view contiguous, bstr") {
    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);

    CHECK(enc(140_tag, wrap_as_array{1, std::array<std::byte, 5>{static_cast<std::byte>('h'), static_cast<std::byte>('e'),
                                                                 static_cast<std::byte>('l'), static_cast<std::byte>('l'),
                                                                 static_cast<std::byte>('o')}}));
    CBOR_TAGS_TEST_LOG("data: {}\n", to_hex(data));

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
    const auto *m1 = static_cast<const std::byte *>(&data[5]);
    const auto *m2 = static_cast<const std::byte *>(b.data());
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
    CHECK(std::ranges::equal(view.view(), str));

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

    CHECK(std::ranges::equal(view.view(), vec));
}

TEST_CASE_TEMPLATE("Test big data chunk view", T, std::deque<char>, std::list<uint8_t>) {
    auto                   data = T{};
    auto                   enc  = make_encoder(data);
    std::vector<std::byte> vec;

    rng::small_generator rng(std::random_device{}());
    std::ranges::generate_n(std::back_inserter(vec), 10000, [&rng]() { return static_cast<std::byte>(rng() % 256); });
    REQUIRE(enc(vec));

    auto dec    = make_decoder(data);
    auto view   = typename decltype(dec)::bstr_view_t{};
    auto result = dec(view);
    REQUIRE(result);

    REQUIRE(std::ranges::equal(view.view(), vec));

    data.back() = static_cast<typename T::value_type>(0xff);
    CHECK_EQ(view.view().back(), std::byte{0xff});
}
