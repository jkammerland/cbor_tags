#include "test_util.h"

#include <array>
#include <bit>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/custom_codec_1.h>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

enum class compact_enum : std::uint16_t { one = 0x0102 };

struct compact_payload {
    std::uint16_t                                   a{};
    std::int32_t                                    b{};
    bool                                            c{};
    double                                          d{};
    compact_enum                                    e{};
    std::optional<std::string>                      label{};
    std::vector<std::int16_t>                       values{};
    std::array<std::byte, 3>                        fixed{};
    std::map<std::uint8_t, std::string>             names{};
    std::variant<std::int32_t, double, std::string> choice{};

    bool operator==(const compact_payload &) const = default;
};

struct compact_non_default_view_payload {
    std::span<const std::byte, 2> bytes;
    std::string_view              label{};
};

struct compact_inline_tag_payload {
    static constexpr std::uint64_t cbor_tag = 33;

    std::uint8_t value{};
    bool         ok{};

    bool operator==(const compact_inline_tag_payload &) const = default;
};

struct compact_edge_leaf {
    std::uint8_t                                                                                           id{};
    std::optional<std::vector<std::uint16_t>>                                                              samples{};
    std::variant<std::string, std::map<std::uint8_t, std::vector<std::int16_t>>, std::array<std::byte, 2>> data{};

    bool operator==(const compact_edge_leaf &) const = default;
};

struct compact_deep_payload {
    std::array<compact_edge_leaf, 2>                                                                   leaves{};
    std::map<std::string, std::optional<std::variant<std::uint8_t, double, std::vector<std::string>>>> lookup{};
    std::vector<std::array<std::byte, 2>>                                                              blobs{};
    bool                                                                                               flag{};

    bool operator==(const compact_deep_payload &) const = default;
};

struct compact_unsized_even_view {
    struct iterator {
        using iterator_concept = std::input_iterator_tag;
        using value_type       = int;
        using difference_type  = std::ptrdiff_t;

        int value{};

        [[nodiscard]] constexpr int operator*() const noexcept { return value; }

        constexpr iterator &operator++() noexcept {
            value += 2;
            return *this;
        }

        constexpr void operator++(int) noexcept { ++(*this); }

        friend constexpr bool operator==(iterator it, std::default_sentinel_t) noexcept { return it.value > 4; }
        friend constexpr bool operator==(std::default_sentinel_t sentinel, iterator it) noexcept { return it == sentinel; }
    };

    [[nodiscard]] constexpr iterator                begin() const noexcept { return {}; }
    [[nodiscard]] constexpr std::default_sentinel_t end() const noexcept { return {}; }
};

template <typename T> void check_compact_wire(const T &in, T out, std::string_view expected_hex) {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<custom_codec_1>(compact);
    REQUIRE(enc(as_compact(static_tag<1>{}, in)));
    CHECK_EQ(to_hex(compact), expected_hex);

    auto dec = make_decoder<custom_codec_1>(compact);
    REQUIRE(dec(as_compact(static_tag<1>{}, out)));
    CHECK(out == in);
}

template <typename T> auto decode_compact_hex(std::string_view hex, T &&value) {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    auto bytes = to_bytes(hex);
    auto dec   = make_decoder<custom_codec_1>(bytes);
    return dec(std::forward<T>(value));
}

[[nodiscard]] std::string repeat_hex(std::string_view chunk, std::size_t count) {
    std::string result;
    result.reserve(chunk.size() * count);
    for (std::size_t i = 0; i < count; ++i) {
        result += chunk;
    }
    return result;
}

} // namespace

static_assert(!std::default_initializable<compact_non_default_view_payload>);
static_assert(cbor::tags::detail::compact::has_borrowed_decode_refs_v<compact_non_default_view_payload>);
static_assert(std::ranges::range<compact_unsized_even_view>);
static_assert(!std::ranges::sized_range<compact_unsized_even_view>);

namespace cbor::tags {
template <> constexpr auto cbor_tag<::compact_payload>() { return static_tag<1000>{}; }
} // namespace cbor::tags

TEST_CASE("compact tagged roundtrips aggregate payload without CBOR field wrappers") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    const compact_payload in{
        .a      = 0x1234,
        .b      = -2,
        .c      = true,
        .d      = 3.5,
        .e      = compact_enum::one,
        .label  = std::string{"hi"},
        .values = {1, -2, 3},
        .fixed  = {std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}},
        .names  = {{1, "one"}, {2, "two"}},
        .choice = std::string{"variant"},
    };

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<custom_codec_1>(compact);
    REQUIRE(enc(as_compact(in)));

    const auto hex = to_hex(compact);
    CHECK_EQ(hex, "d903e858333412feffffff010000000000000c40020101026869030100feff0300aabbcc0201036f6e65020374776f020776617269616e74");
    CHECK(hex.find("84") == std::string::npos);

    compact_payload out{};
    auto            dec = make_decoder<custom_codec_1>(compact);
    REQUIRE(dec(as_compact(out)));
    CHECK(out == in);
}

TEST_CASE("compact tagged roundtrips deep nested aggregate schemas") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    compact_deep_payload in{
        .leaves =
            {
                compact_edge_leaf{
                    .id      = 1,
                    .samples = std::vector<std::uint16_t>{0x1234, 0xABCD},
                    .data    = std::map<std::uint8_t, std::vector<std::int16_t>>{{2, {1, -2, 3}}, {9, {-4}}},
                },
                compact_edge_leaf{
                    .id      = 2,
                    .samples = std::nullopt,
                    .data    = std::array<std::byte, 2>{std::byte{0xAA}, std::byte{0xBB}},
                },
            },
        .lookup =
            {
                {"alpha", std::variant<std::uint8_t, double, std::vector<std::string>>{std::uint8_t{7}}},
                {"beta", std::variant<std::uint8_t, double, std::vector<std::string>>{double{2.5}}},
                {"gamma", std::variant<std::uint8_t, double, std::vector<std::string>>{std::vector<std::string>{"x", "yy"}}},
                {"none", std::nullopt},
            },
        .blobs = {std::array<std::byte, 2>{std::byte{0x10}, std::byte{0x20}}, std::array<std::byte, 2>{std::byte{0x30}, std::byte{0x40}}},
        .flag  = true,
    };

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<custom_codec_1>(compact);
    REQUIRE(enc(as_compact(static_tag<222>{}, in)));
    CHECK(to_hex(compact).starts_with("d8de58"));

    compact_deep_payload out{};
    auto                 dec = make_decoder<custom_codec_1>(compact);
    REQUIRE(dec(as_compact(static_tag<222>{}, out)));
    CHECK(out == in);
}

TEST_CASE("compact tagged pins exact nested tuple map optional variant wire") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    using left_type  = std::map<std::uint8_t, std::optional<std::variant<std::uint16_t, std::string>>>;
    using right_type = std::optional<std::vector<std::variant<std::uint8_t, std::string>>>;
    using payload    = std::tuple<left_type, right_type>;

    const payload in{
        left_type{{1, std::nullopt}, {2, std::variant<std::uint16_t, std::string>{std::string{"xy"}}}},
        std::vector<std::variant<std::uint8_t, std::string>>{std::variant<std::uint8_t, std::string>{std::uint8_t{7}},
                                                             std::variant<std::uint8_t, std::string>{std::string{"z"}}},
    };

    check_compact_wire(in, payload{}, "c1500201000201010278790102000701017a");
}

TEST_CASE("compact tagged explicit tag encodes typed arrays without per-element float markers") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    const std::vector<double> values{1.0, 2.0, 4.0};

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<custom_codec_1>(compact);
    REQUIRE(enc(as_compact(static_tag<77>{}, values)));

    const auto hex = to_hex(compact);
    CHECK(hex.starts_with("d84d5819"));
    CHECK(hex.find("fb") == std::string::npos);

    std::vector<double> decoded;
    auto                dec = make_decoder<custom_codec_1>(compact);
    REQUIRE(dec(as_compact(static_tag<77>{}, decoded)));
    CHECK(decoded == values);
}

TEST_CASE("compact tagged scalar payloads have stable minimal wire shapes") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    check_compact_wire(std::byte{0xAA}, std::byte{}, "c141aa");
    check_compact_wire(std::uint16_t{0x1234}, std::uint16_t{}, "c1423412");
    check_compact_wire(std::int32_t{-2}, std::int32_t{}, "c144feffffff");
    check_compact_wire(float{1.0F}, float{}, "c1440000803f");
    check_compact_wire(negative{5}, negative{}, "c1480500000000000000");
    check_compact_wire(integer{5, true}, integer{0}, "c149010500000000000000");
    check_compact_wire(simple{16}, simple{}, "c14110");
    check_compact_wire(nullptr, nullptr, "c140");

    float16_t              in{std::uint16_t{0x3C00}};
    float16_t              out{};
    std::vector<std::byte> compact;
    auto                   enc = make_encoder<custom_codec_1>(compact);
    REQUIRE(enc(as_compact(static_tag<1>{}, in)));
    CHECK_EQ(to_hex(compact), "c142003c");

    auto dec = make_decoder<custom_codec_1>(compact);
    REQUIRE(dec(as_compact(static_tag<1>{}, out)));
    CHECK(out.value == in.value);
}

TEST_CASE("compact tagged scalar extremes are bit exact") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    check_compact_wire(std::numeric_limits<std::int64_t>::min(), std::int64_t{}, "c1480000000000000080");
    check_compact_wire(std::numeric_limits<std::uint64_t>::max(), std::uint64_t{}, "c148ffffffffffffffff");

    const auto nan_bits = std::uint64_t{0x7FF8000012345678ULL};
    const auto in       = std::bit_cast<double>(nan_bits);

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<custom_codec_1>(compact);
    REQUIRE(enc(as_compact(static_tag<1>{}, in)));
    CHECK_EQ(to_hex(compact), "c148785634120000f87f");

    double out{};
    auto   dec = make_decoder<custom_codec_1>(compact);
    REQUIRE(dec(as_compact(static_tag<1>{}, out)));
    CHECK(std::bit_cast<std::uint64_t>(out) == nan_bits);
}

TEST_CASE("compact tagged fixed arrays omit compact length prefixes") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    check_compact_wire(std::array<std::byte, 3>{std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}}, std::array<std::byte, 3>{},
                       "c143aabbcc");
    check_compact_wire(std::array<std::uint16_t, 2>{std::uint16_t{0x1234}, std::uint16_t{0xABCD}}, std::array<std::uint16_t, 2>{},
                       "c1443412cdab");
}

TEST_CASE("compact tagged zero length payloads are still schema-specific") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    check_compact_wire(std::array<std::byte, 0>{}, std::array<std::byte, 0>{}, "c140");
    check_compact_wire(std::string{}, std::string{"not empty"}, "c14100");
    check_compact_wire(std::vector<std::uint16_t>{}, std::vector<std::uint16_t>{1, 2}, "c14100");
    check_compact_wire(std::map<std::uint8_t, std::string>{}, std::map<std::uint8_t, std::string>{{1, "old"}}, "c14100");
}

TEST_CASE("compact tagged dynamic tags use the same compact payload core") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    const std::uint8_t value{7};

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<custom_codec_1>(compact);
    REQUIRE(enc(as_compact(dynamic_tag<std::uint16_t>{300}, value)));
    CHECK_EQ(to_hex(compact), "d9012c4107");

    std::uint8_t decoded{};
    auto         dec = make_decoder<custom_codec_1>(compact);
    REQUIRE(dec(as_compact(dynamic_tag<std::uint16_t>{300}, decoded)));
    CHECK(decoded == value);
}

TEST_CASE("compact tagged long tag and length boundaries stay compact") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    {
        const bool             value{true};
        std::vector<std::byte> compact;
        auto                   enc = make_encoder<custom_codec_1>(compact);
        REQUIRE(enc(as_compact(static_tag<0x100000000ULL>{}, value)));
        CHECK_EQ(to_hex(compact), "db00000001000000004101");

        bool decoded{};
        auto dec = make_decoder<custom_codec_1>(compact);
        REQUIRE(dec(as_compact(static_tag<0x100000000ULL>{}, decoded)));
        CHECK(decoded == value);
    }
    {
        const std::string      text(127, 'a');
        std::vector<std::byte> compact;
        auto                   enc = make_encoder<custom_codec_1>(compact);
        REQUIRE(enc(as_compact(static_tag<1>{}, text)));
        CHECK_EQ(to_hex(compact), std::string{"c158807f"} + repeat_hex("61", 127));

        std::string decoded;
        auto        dec = make_decoder<custom_codec_1>(compact);
        REQUIRE(dec(as_compact(static_tag<1>{}, decoded)));
        CHECK(decoded == text);
    }
    {
        const std::string      text(128, 'x');
        std::vector<std::byte> compact;
        auto                   enc = make_encoder<custom_codec_1>(compact);
        REQUIRE(enc(as_compact(static_tag<1>{}, text)));
        CHECK_EQ(to_hex(compact), std::string{"c158828001"} + repeat_hex("78", 128));

        std::string decoded;
        auto        dec = make_decoder<custom_codec_1>(compact);
        REQUIRE(dec(as_compact(static_tag<1>{}, decoded)));
        CHECK(decoded == text);
    }
    {
        std::vector<std::uint8_t> values(128);
        for (std::uint8_t i = 0; i < values.size(); ++i) {
            values[i] = i;
        }

        std::vector<std::byte> compact;
        auto                   enc = make_encoder<custom_codec_1>(compact);
        REQUIRE(enc(as_compact(static_tag<1>{}, values)));
        CHECK(to_hex(compact).starts_with("c158828001"));

        std::vector<std::uint8_t> decoded;
        auto                      dec = make_decoder<custom_codec_1>(compact);
        REQUIRE(dec(as_compact(static_tag<1>{}, decoded)));
        CHECK(decoded == values);
    }
    {
        const std::vector<std::uint16_t> values(127, 0x1234);
        std::vector<std::byte>           compact;
        auto                             enc = make_encoder<custom_codec_1>(compact);
        REQUIRE(enc(as_compact(static_tag<1>{}, values)));
        CHECK_EQ(to_hex(compact), std::string{"c158ff7f"} + repeat_hex("3412", 127));

        std::vector<std::uint16_t> decoded;
        auto                       dec = make_decoder<custom_codec_1>(compact);
        REQUIRE(dec(as_compact(static_tag<1>{}, decoded)));
        CHECK(decoded == values);
    }
    {
        const std::vector<std::uint16_t> values(128, 0x1234);
        std::vector<std::byte>           compact;
        auto                             enc = make_encoder<custom_codec_1>(compact);
        REQUIRE(enc(as_compact(static_tag<1>{}, values)));
        CHECK_EQ(to_hex(compact), std::string{"c15901028001"} + repeat_hex("3412", 128));

        std::vector<std::uint16_t> decoded;
        auto                       dec = make_decoder<custom_codec_1>(compact);
        REQUIRE(dec(as_compact(static_tag<1>{}, decoded)));
        CHECK(decoded == values);
    }
}

TEST_CASE("compact tagged supports existing tag pair idiom") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    auto tagged = make_tag_pair(static_tag<123>{}, std::vector<std::uint16_t>{0x1234, 0xABCD});

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<custom_codec_1>(compact);
    REQUIRE(enc(as_compact(tagged)));
    CHECK_EQ(to_hex(compact), "d87b45023412cdab");

    tagged_object<static_tag<123>, std::vector<std::uint16_t>> decoded{};
    auto                                                       dec = make_decoder<custom_codec_1>(compact);
    REQUIRE(dec(as_compact(decoded)));
    CHECK(decoded.second == tagged.second);
}

TEST_CASE("compact tagged supports plain and tagged tuple payloads") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    check_compact_wire(std::tuple<std::uint8_t, bool>{7, true}, std::tuple<std::uint8_t, bool>{}, "c1420701");

    const auto             tagged = std::tuple{static_tag<31>{}, std::uint8_t{7}, true};
    std::vector<std::byte> compact;
    auto                   enc = make_encoder<custom_codec_1>(compact);
    REQUIRE(enc(as_compact(tagged)));
    CHECK_EQ(to_hex(compact), "d81f420701");

    std::tuple<static_tag<31>, std::uint8_t, bool> decoded{};
    auto                                           dec = make_decoder<custom_codec_1>(compact);
    REQUIRE(dec(as_compact(decoded)));
    CHECK(std::get<1>(decoded) == 7);
    CHECK(std::get<2>(decoded));
}

TEST_CASE("compact tagged infers inline aggregate tags") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    const compact_inline_tag_payload in{.value = 7, .ok = true};

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<custom_codec_1>(compact);
    REQUIRE(enc(as_compact(in)));
    CHECK_EQ(to_hex(compact), "d821420701");

    compact_inline_tag_payload out{};
    auto                       dec = make_decoder<custom_codec_1>(compact);
    REQUIRE(dec(as_compact(out)));
    CHECK(out == in);
}

TEST_CASE("compact tagged decode composes after the initial byte is already consumed") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    const std::uint16_t value{0x1234};

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<custom_codec_1>(compact);
    REQUIRE(enc(as_compact(static_tag<1>{}, value)));

    std::uint16_t decoded{};
    auto          dec             = make_decoder<custom_codec_1>(compact);
    auto [major, additional_info] = dec.read_initial_byte();
    auto result                   = dec.decode(as_compact(static_tag<1>{}, decoded), major, additional_info);
    CHECK(result == status_code::success);
    CHECK(decoded == value);
}

TEST_CASE("compact tagged consumed-initial-byte decode reports malformed envelopes without throwing") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    {
        auto          compact = to_bytes("d9");
        std::uint16_t decoded{};
        auto          dec             = make_decoder<custom_codec_1>(compact);
        auto [major, additional_info] = dec.read_initial_byte();
        status_code status{status_code::success};
        CHECK_NOTHROW(status = dec.decode(as_compact(static_tag<1>{}, decoded), major, additional_info));
        CHECK(status == status_code::incomplete);
    }
    {
        auto          compact = to_bytes("c15802aa");
        std::uint16_t decoded{};
        auto          dec             = make_decoder<custom_codec_1>(compact);
        auto [major, additional_info] = dec.read_initial_byte();
        status_code status{status_code::success};
        CHECK_NOTHROW(status = dec.decode(as_compact(static_tag<1>{}, decoded), major, additional_info));
        CHECK(status == status_code::incomplete);
    }
}

TEST_CASE("compact tagged wrong tag rejects before payload decode") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    const std::vector<std::uint16_t> values{1, 2, 3};
    std::vector<std::byte>           compact;
    auto                             enc = make_encoder<custom_codec_1>(compact);
    REQUIRE(enc(as_compact(static_tag<44>{}, values)));

    std::vector<std::uint16_t> decoded;
    auto                       dec    = make_decoder<custom_codec_1>(compact);
    auto                       result = dec(as_compact(static_tag<45>{}, decoded));
    REQUIRE_FALSE(result);
    CHECK(result.error() == status_code::no_match_for_tag);
    CHECK(decoded.empty());
}

TEST_CASE("compact tagged malformed envelope metadata is rejected") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    {
        bool out{};
        auto result = decode_compact_hex("00", as_compact(static_tag<1>{}, out));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::no_match_for_tag_on_buffer);
    }
    {
        bool out{};
        auto result = decode_compact_hex("dc", as_compact(static_tag<1>{}, out));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::error);
    }
    {
        bool out{};
        auto result = decode_compact_hex("c1", as_compact(static_tag<1>{}, out));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::incomplete);
    }
    {
        bool out{};
        auto result = decode_compact_hex("c180", as_compact(static_tag<1>{}, out));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::no_match_for_bstr_on_buffer);
    }
    {
        bool out{};
        auto result = decode_compact_hex("c15fff", as_compact(static_tag<1>{}, out));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::no_match_for_bstr_on_buffer);
    }
    {
        bool out{};
        auto result = decode_compact_hex("c15c", as_compact(static_tag<1>{}, out));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::error);
    }
    {
        bool out{};
        auto result = decode_compact_hex("c159", as_compact(static_tag<1>{}, out));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::incomplete);
    }
    {
        bool out{};
        auto result = decode_compact_hex("c14201", as_compact(static_tag<1>{}, out));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::incomplete);
    }
    {
        bool out{};
        auto result = decode_compact_hex("c1420100", as_compact(static_tag<1>{}, out));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::error);
    }
}

TEST_CASE("compact tagged malformed bool is rejected") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    struct bool_payload {
        bool value{};
    };

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<custom_codec_1>(compact);
    const bool_payload     in{.value = true};
    REQUIRE(enc(as_compact(static_tag<9>{}, in)));

    REQUIRE(compact.size() >= 3);
    compact.back() = std::byte{0x02};

    bool_payload out{};
    auto         dec    = make_decoder<custom_codec_1>(compact);
    auto         result = dec(as_compact(static_tag<9>{}, out));
    REQUIRE_FALSE(result);
    CHECK(result.error() == status_code::error);
}

TEST_CASE("compact tagged malformed optional payload leaves destination unchanged") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    {
        std::optional<std::uint16_t> decoded{0xBEEF};
        auto                         result = decode_compact_hex("c14100", as_compact(static_tag<1>{}, decoded));
        REQUIRE(result);
        CHECK_FALSE(decoded.has_value());
    }
    {
        std::optional<std::uint16_t> decoded{0xBEEF};
        auto                         result = decode_compact_hex("c1420134", as_compact(static_tag<1>{}, decoded));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::incomplete);
        REQUIRE(decoded.has_value());
        CHECK(*decoded == 0xBEEF);
    }
    {
        std::optional<std::uint16_t> decoded{0xBEEF};
        auto                         result = decode_compact_hex("c14102", as_compact(static_tag<1>{}, decoded));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::error);
        REQUIRE(decoded.has_value());
        CHECK(*decoded == 0xBEEF);
    }
    {
        std::optional<std::uint16_t> decoded{0xBEEF};
        auto                         result = decode_compact_hex("c14200ff", as_compact(static_tag<1>{}, decoded));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::error);
        REQUIRE(decoded.has_value());
        CHECK(*decoded == 0xBEEF);
    }
}

TEST_CASE("compact tagged rejects malformed compact lengths and variant indexes") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    {
        std::vector<std::uint16_t> decoded;
        auto                       result = decode_compact_hex("c14180", as_compact(static_tag<1>{}, decoded));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::incomplete);
    }
    {
        std::vector<std::uint16_t> decoded;
        auto                       result = decode_compact_hex("c14a80808080808080808002", as_compact(static_tag<1>{}, decoded));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::error);
    }
    {
        std::variant<std::uint8_t, std::string> decoded;
        auto                                    result = decode_compact_hex("c14102", as_compact(static_tag<1>{}, decoded));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::no_match_in_variant_on_buffer);
    }
    {
        std::variant<std::uint8_t, std::string> decoded;
        auto result = decode_compact_hex("c14affffffffffffffffff01", as_compact(static_tag<1>{}, decoded));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::no_match_in_variant_on_buffer);
    }
}

TEST_CASE("compact tagged malformed containers leave destinations unchanged") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    {
        std::vector<std::uint16_t> decoded{0xBEEF};
        auto                       result = decode_compact_hex("c143023412", as_compact(static_tag<1>{}, decoded));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::incomplete);
        CHECK(decoded == std::vector<std::uint16_t>{0xBEEF});
    }
    {
        std::map<std::uint8_t, std::uint16_t> decoded{{9, 0xBEEF}};
        auto                                  result = decode_compact_hex("c1450201341202", as_compact(static_tag<1>{}, decoded));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::incomplete);
        CHECK(decoded == std::map<std::uint8_t, std::uint16_t>{{9, 0xBEEF}});
    }
    {
        std::map<std::uint8_t, std::string> decoded{{9, "keep"}};
        auto                                result = decode_compact_hex("c14702010161020262", as_compact(static_tag<1>{}, decoded));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::incomplete);
        CHECK(decoded == std::map<std::uint8_t, std::string>{{9, "keep"}});
    }
}

TEST_CASE("compact tagged map decode handles repeated keys according to container semantics") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    {
        std::map<std::uint8_t, std::uint16_t> decoded;
        auto                                  result = decode_compact_hex("c14702011111012222", as_compact(static_tag<1>{}, decoded));
        REQUIRE(result);
        REQUIRE(decoded.size() == 1);
        CHECK(decoded.at(1) == 0x2222);
    }
    {
        std::multimap<std::uint8_t, std::uint16_t> decoded;
        auto                                       result = decode_compact_hex("c14702011111012222", as_compact(static_tag<1>{}, decoded));
        REQUIRE(result);
        CHECK(decoded.count(1) == 2);
    }
}

TEST_CASE("compact tagged variants roundtrip primitive alternatives by index") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    using payload = std::variant<std::uint8_t, double, std::string>;

    check_compact_wire(payload{std::uint8_t{7}}, payload{}, "c1420007");
    check_compact_wire(payload{double{1.0}}, payload{}, "c14901000000000000f03f");
    check_compact_wire(payload{std::string{"hi"}}, payload{}, "c14402026869");
}

TEST_CASE("compact tagged byte-like strings use binary compact dispatch") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    const std::basic_string<std::byte> in{std::byte{0xAA}, std::byte{0xBB}};
    std::basic_string<std::byte>       out;

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<custom_codec_1>(compact);
    REQUIRE(enc(as_compact(static_tag<1>{}, in)));
    CHECK_EQ(to_hex(compact), "c14302aabb");

    auto dec = make_decoder<custom_codec_1>(compact);
    REQUIRE(dec(as_compact(static_tag<1>{}, out)));
    CHECK(out == in);
}

TEST_CASE("compact tagged materializes unsized input views before encoding") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    const compact_unsized_even_view evens;

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<custom_codec_1>(compact);
    REQUIRE(enc(as_compact(static_tag<1>{}, evens)));
    CHECK_EQ(to_hex(compact), "c14d03000000000200000004000000");

    std::vector<int> decoded;
    auto             dec = make_decoder<custom_codec_1>(compact);
    REQUIRE(dec(as_compact(static_tag<1>{}, decoded)));
    CHECK(decoded == std::vector<int>{0, 2, 4});
}

TEST_CASE("compact tagged rejects borrowed views from non-contiguous payload storage") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    struct view_payload {
        std::string_view label{};
    };

    const view_payload in{.label = "borrowed"};

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<custom_codec_1>(compact);
    REQUIRE(enc(as_compact(static_tag<12>{}, in)));

    const std::deque<std::byte> non_contiguous(compact.begin(), compact.end());
    view_payload                out{};
    auto                        dec    = make_decoder<custom_codec_1>(non_contiguous);
    auto                        result = dec(as_compact(static_tag<12>{}, out));
    REQUIRE_FALSE(result);
    CHECK(result.error() == status_code::contiguous_view_on_non_contiguous_data);
}

TEST_CASE("compact tagged borrowed views inside containers decode only from contiguous storage") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    const std::vector<std::string_view> in{"a", "bb"};

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<custom_codec_1>(compact);
    REQUIRE(enc(as_compact(static_tag<19>{}, in)));
    CHECK_EQ(to_hex(compact), "d346020161026262");

    std::vector<std::string_view> decoded;
    auto                          dec = make_decoder<custom_codec_1>(compact);
    REQUIRE(dec(as_compact(static_tag<19>{}, decoded)));
    CHECK(decoded == in);
    for (const auto view : decoded) {
        CHECK(view.data() >= reinterpret_cast<const char *>(compact.data()));
        CHECK(view.data() + view.size() <= reinterpret_cast<const char *>(compact.data() + compact.size()));
    }

    const std::deque<std::byte> non_contiguous(compact.begin(), compact.end());
    decoded.clear();
    auto non_contiguous_dec = make_decoder<custom_codec_1>(non_contiguous);
    auto result             = non_contiguous_dec(as_compact(static_tag<19>{}, decoded));
    REQUIRE_FALSE(result);
    CHECK(result.error() == status_code::contiguous_view_on_non_contiguous_data);

    const std::map<std::string_view, std::uint8_t> map_in{{"k", 7}};
    compact.clear();
    auto map_enc = make_encoder<custom_codec_1>(compact);
    REQUIRE(map_enc(as_compact(static_tag<1>{}, map_in)));
    CHECK_EQ(to_hex(compact), "c14401016b07");

    std::map<std::string_view, std::uint8_t> map_decoded;
    auto                                     map_dec = make_decoder<custom_codec_1>(compact);
    REQUIRE(map_dec(as_compact(static_tag<1>{}, map_decoded)));
    REQUIRE(map_decoded.size() == 1);
    CHECK(map_decoded.begin()->first == "k");
    CHECK(map_decoded.begin()->first.data() >= reinterpret_cast<const char *>(compact.data()));
    CHECK(map_decoded.begin()->first.data() + map_decoded.begin()->first.size() <=
          reinterpret_cast<const char *>(compact.data() + compact.size()));
    CHECK(map_decoded.begin()->second == 7);

    const std::deque<std::byte> non_contiguous_map(compact.begin(), compact.end());
    map_decoded.clear();
    auto non_contiguous_map_dec = make_decoder<custom_codec_1>(non_contiguous_map);
    result                      = non_contiguous_map_dec(as_compact(static_tag<1>{}, map_decoded));
    REQUIRE_FALSE(result);
    CHECK(result.error() == status_code::contiguous_view_on_non_contiguous_data);
}

TEST_CASE("compact tagged borrowed views decode from contiguous payload storage") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    {
        const std::string_view in{"borrowed"};
        std::vector<std::byte> compact;
        auto                   enc = make_encoder<custom_codec_1>(compact);
        REQUIRE(enc(as_compact(static_tag<16>{}, in)));

        std::string_view out;
        auto             dec = make_decoder<custom_codec_1>(compact);
        REQUIRE(dec(as_compact(static_tag<16>{}, out)));
        CHECK(out == in);
        CHECK(out.data() >= reinterpret_cast<const char *>(compact.data()));
        CHECK(out.data() + out.size() <= reinterpret_cast<const char *>(compact.data() + compact.size()));
    }
    {
        const std::vector<std::byte> in{std::byte{0xAA}, std::byte{0xBB}};
        std::span<const std::byte>   in_view{in};
        std::vector<std::byte>       compact;
        auto                         enc = make_encoder<custom_codec_1>(compact);
        REQUIRE(enc(as_compact(static_tag<17>{}, in_view)));

        std::span<const std::byte> out;
        auto                       dec = make_decoder<custom_codec_1>(compact);
        REQUIRE(dec(as_compact(static_tag<17>{}, out)));
        CHECK(to_hex(out) == "aabb");
        CHECK(out.data() >= compact.data());
        CHECK(out.data() + out.size() <= compact.data() + compact.size());
    }
}

TEST_CASE("compact tagged borrowed view detection handles nested and non-default aggregates") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    {
        const std::optional<std::string_view> in{"borrowed"};
        std::vector<std::byte>                compact;
        auto                                  enc = make_encoder<custom_codec_1>(compact);
        REQUIRE(enc(as_compact(static_tag<13>{}, in)));

        const std::deque<std::byte>     non_contiguous(compact.begin(), compact.end());
        std::optional<std::string_view> out{};
        auto                            dec    = make_decoder<custom_codec_1>(non_contiguous);
        auto                            result = dec(as_compact(static_tag<13>{}, out));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::contiguous_view_on_non_contiguous_data);
    }
    {
        std::array<std::byte, 2>               in_bytes{std::byte{0x12}, std::byte{0x34}};
        const compact_non_default_view_payload in{.bytes = std::span<const std::byte, 2>{in_bytes}, .label = "borrowed"};
        std::vector<std::byte>                 compact;
        auto                                   enc = make_encoder<custom_codec_1>(compact);
        REQUIRE(enc(as_compact(static_tag<14>{}, in)));

        const std::deque<std::byte>      non_contiguous(compact.begin(), compact.end());
        std::array<std::byte, 2>         out_bytes{};
        compact_non_default_view_payload out{.bytes = std::span<const std::byte, 2>{out_bytes}, .label = {}};
        auto                             dec    = make_decoder<custom_codec_1>(non_contiguous);
        auto                             result = dec(as_compact(static_tag<14>{}, out));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::contiguous_view_on_non_contiguous_data);
    }
    {
        std::array<std::byte, 2>         backing{std::byte{0xCC}, std::byte{0xDD}};
        compact_non_default_view_payload out{.bytes = std::span<const std::byte, 2>{backing}, .label = "old"};
        auto                             result = decode_compact_hex("c14502aabb0278", as_compact(static_tag<1>{}, out));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::incomplete);
        CHECK(out.bytes.data() == backing.data());
        CHECK(to_hex(out.bytes) == "ccdd");
        CHECK(out.label == "old");
    }
}

TEST_CASE("compact tagged owning values decode from non-contiguous payload storage") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    const std::vector<std::uint16_t> values{0x1234, 0xABCD};
    std::vector<std::byte>           compact;
    auto                             enc = make_encoder<custom_codec_1>(compact);
    REQUIRE(enc(as_compact(static_tag<15>{}, values)));

    const std::deque<std::byte> non_contiguous(compact.begin(), compact.end());
    std::vector<std::uint16_t>  decoded;
    auto                        dec = make_decoder<custom_codec_1>(non_contiguous);
    REQUIRE(dec(as_compact(static_tag<15>{}, decoded)));
    CHECK(decoded == values);
}

TEST_CASE("compact tagged non-contiguous containers roundtrip through compact ranges") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    const std::deque<std::uint16_t> values{0x1234, 0xABCD};

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<custom_codec_1>(compact);
    REQUIRE(enc(as_compact(static_tag<18>{}, values)));
    CHECK_EQ(to_hex(compact), "d245023412cdab");

    std::deque<std::uint16_t> decoded;
    auto                      dec = make_decoder<custom_codec_1>(compact);
    REQUIRE(dec(as_compact(static_tag<18>{}, decoded)));
    CHECK(decoded == values);
}

TEST_CASE("compact tagged fixed borrowed binary spans validate compact length") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    std::array<std::byte, 2>      backing{std::byte{0xCC}, std::byte{0xDD}};
    std::span<const std::byte, 2> decoded{backing};
    auto                          result = decode_compact_hex("c14201aa", as_compact(static_tag<1>{}, decoded));
    REQUIRE_FALSE(result);
    CHECK(result.error() == status_code::unexpected_group_size);
    CHECK(decoded.data() == backing.data());
}

TEST_CASE("compact tagged truncated payload reports incomplete") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    const std::vector<std::uint32_t> values{1, 2, 3};
    std::vector<std::byte>           compact;
    auto                             enc = make_encoder<custom_codec_1>(compact);
    REQUIRE(enc(as_compact(static_tag<11>{}, values)));

    compact.pop_back();

    std::vector<std::uint32_t> decoded;
    auto                       dec    = make_decoder<custom_codec_1>(compact);
    auto                       result = dec(as_compact(static_tag<11>{}, decoded));
    REQUIRE_FALSE(result);
    CHECK(result.error() == status_code::incomplete);
}
