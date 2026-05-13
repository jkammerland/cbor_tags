#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/compact_tagged.h>
#include <doctest/doctest.h>

#include "test_util.h"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

enum class compact_enum : std::uint16_t { one = 0x0102 };

struct compact_payload {
    std::uint16_t            a{};
    std::int32_t             b{};
    bool                     c{};
    double                   d{};
    compact_enum             e{};
    std::optional<std::string> label{};
    std::vector<std::int16_t>  values{};
    std::array<std::byte, 3>   fixed{};
    std::map<std::uint8_t, std::string> names{};
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

template <typename T> void check_compact_wire(const T &in, T out, std::string_view expected_hex) {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<compact_tagged_codec>(compact);
    REQUIRE(enc(as_compact(static_tag<1>{}, in)));
    CHECK_EQ(to_hex(compact), expected_hex);

    auto dec = make_decoder<compact_tagged_codec>(compact);
    REQUIRE(dec(as_compact(static_tag<1>{}, out)));
    CHECK(out == in);
}

template <typename T> auto decode_compact_hex(std::string_view hex, T &&value) {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    auto bytes = to_bytes(hex);
    auto dec   = make_decoder<compact_tagged_codec>(bytes);
    return dec(std::forward<T>(value));
}

} // namespace

static_assert(!std::default_initializable<compact_non_default_view_payload>);
static_assert(cbor::tags::detail::compact::has_borrowed_decode_refs_v<compact_non_default_view_payload>);

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
    auto                   enc = make_encoder<compact_tagged_codec>(compact);
    REQUIRE(enc(as_compact(in)));

    const auto hex = to_hex(compact);
    CHECK(hex.starts_with("d903e858"));
    CHECK(hex.find("84") == std::string::npos);

    compact_payload out{};
    auto            dec = make_decoder<compact_tagged_codec>(compact);
    REQUIRE(dec(as_compact(out)));
    CHECK(out == in);
}

TEST_CASE("compact tagged explicit tag encodes typed arrays without per-element float markers") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    const std::vector<double> values{1.0, 2.0, 4.0};

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<compact_tagged_codec>(compact);
    REQUIRE(enc(as_compact(static_tag<77>{}, values)));

    const auto hex = to_hex(compact);
    CHECK(hex.starts_with("d84d5819"));
    CHECK(hex.find("fb") == std::string::npos);

    std::vector<double> decoded;
    auto                dec = make_decoder<compact_tagged_codec>(compact);
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

    float16_t in{std::uint16_t{0x3C00}};
    float16_t out{};
    std::vector<std::byte> compact;
    auto                   enc = make_encoder<compact_tagged_codec>(compact);
    REQUIRE(enc(as_compact(static_tag<1>{}, in)));
    CHECK_EQ(to_hex(compact), "c142003c");

    auto dec = make_decoder<compact_tagged_codec>(compact);
    REQUIRE(dec(as_compact(static_tag<1>{}, out)));
    CHECK(out.value == in.value);
}

TEST_CASE("compact tagged fixed arrays omit compact length prefixes") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    check_compact_wire(
        std::array<std::byte, 3>{std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}}, std::array<std::byte, 3>{}, "c143aabbcc");
    check_compact_wire(
        std::array<std::uint16_t, 2>{std::uint16_t{0x1234}, std::uint16_t{0xABCD}}, std::array<std::uint16_t, 2>{}, "c1443412cdab");
}

TEST_CASE("compact tagged dynamic tags use the same compact payload core") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    const std::uint8_t value{7};

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<compact_tagged_codec>(compact);
    REQUIRE(enc(as_compact(dynamic_tag<std::uint16_t>{300}, value)));
    CHECK_EQ(to_hex(compact), "d9012c4107");

    std::uint8_t decoded{};
    auto         dec = make_decoder<compact_tagged_codec>(compact);
    REQUIRE(dec(as_compact(dynamic_tag<std::uint16_t>{300}, decoded)));
    CHECK(decoded == value);
}

TEST_CASE("compact tagged supports existing tag pair idiom") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    auto tagged = make_tag_pair(static_tag<123>{}, std::vector<std::uint16_t>{0x1234, 0xABCD});

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<compact_tagged_codec>(compact);
    REQUIRE(enc(as_compact(tagged)));
    CHECK_EQ(to_hex(compact), "d87b45023412cdab");

    tagged_object<static_tag<123>, std::vector<std::uint16_t>> decoded{};
    auto                                                       dec = make_decoder<compact_tagged_codec>(compact);
    REQUIRE(dec(as_compact(decoded)));
    CHECK(decoded.second == tagged.second);
}

TEST_CASE("compact tagged supports plain and tagged tuple payloads") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    check_compact_wire(std::tuple<std::uint8_t, bool>{7, true}, std::tuple<std::uint8_t, bool>{}, "c1420701");

    const auto tagged = std::tuple{static_tag<31>{}, std::uint8_t{7}, true};
    std::vector<std::byte> compact;
    auto                   enc = make_encoder<compact_tagged_codec>(compact);
    REQUIRE(enc(as_compact(tagged)));
    CHECK_EQ(to_hex(compact), "d81f420701");

    std::tuple<static_tag<31>, std::uint8_t, bool> decoded{};
    auto                                           dec = make_decoder<compact_tagged_codec>(compact);
    REQUIRE(dec(as_compact(decoded)));
    CHECK(std::get<1>(decoded) == 7);
    CHECK(std::get<2>(decoded));
}

TEST_CASE("compact tagged infers inline aggregate tags") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    const compact_inline_tag_payload in{.value = 7, .ok = true};

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<compact_tagged_codec>(compact);
    REQUIRE(enc(as_compact(in)));
    CHECK_EQ(to_hex(compact), "d821420701");

    compact_inline_tag_payload out{};
    auto                       dec = make_decoder<compact_tagged_codec>(compact);
    REQUIRE(dec(as_compact(out)));
    CHECK(out == in);
}

TEST_CASE("compact tagged decode composes after the initial byte is already consumed") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    const std::uint16_t value{0x1234};

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<compact_tagged_codec>(compact);
    REQUIRE(enc(as_compact(static_tag<1>{}, value)));

    std::uint16_t decoded{};
    auto          dec = make_decoder<compact_tagged_codec>(compact);
    auto          [major, additional_info] = dec.read_initial_byte();
    auto          result                   = dec.decode(as_compact(static_tag<1>{}, decoded), major, additional_info);
    CHECK(result == status_code::success);
    CHECK(decoded == value);
}

TEST_CASE("compact tagged consumed-initial-byte decode reports malformed envelopes without throwing") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    {
        auto          compact = to_bytes("d9");
        std::uint16_t decoded{};
        auto          dec = make_decoder<compact_tagged_codec>(compact);
        auto          [major, additional_info] = dec.read_initial_byte();
        status_code   status{status_code::success};
        CHECK_NOTHROW(status = dec.decode(as_compact(static_tag<1>{}, decoded), major, additional_info));
        CHECK(status == status_code::incomplete);
    }
    {
        auto          compact = to_bytes("c15802aa");
        std::uint16_t decoded{};
        auto          dec = make_decoder<compact_tagged_codec>(compact);
        auto          [major, additional_info] = dec.read_initial_byte();
        status_code   status{status_code::success};
        CHECK_NOTHROW(status = dec.decode(as_compact(static_tag<1>{}, decoded), major, additional_info));
        CHECK(status == status_code::incomplete);
    }
}

TEST_CASE("compact tagged wrong tag rejects before payload decode") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    const std::vector<std::uint16_t> values{1, 2, 3};
    std::vector<std::byte>           compact;
    auto                             enc = make_encoder<compact_tagged_codec>(compact);
    REQUIRE(enc(as_compact(static_tag<44>{}, values)));

    std::vector<std::uint16_t> decoded;
    auto                       dec    = make_decoder<compact_tagged_codec>(compact);
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
    auto                   enc = make_encoder<compact_tagged_codec>(compact);
    const bool_payload     in{.value = true};
    REQUIRE(enc(as_compact(static_tag<9>{}, in)));

    REQUIRE(compact.size() >= 3);
    compact.back() = std::byte{0x02};

    bool_payload out{};
    auto         dec    = make_decoder<compact_tagged_codec>(compact);
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
}

TEST_CASE("compact tagged variants roundtrip primitive alternatives by index") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    using payload = std::variant<std::uint8_t, double, std::string>;

    check_compact_wire(payload{std::uint8_t{7}}, payload{}, "c1420007");
    check_compact_wire(payload{double{1.0}}, payload{}, "c14901000000000000f03f");
}

TEST_CASE("compact tagged rejects borrowed views from non-contiguous payload storage") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    struct view_payload {
        std::string_view label{};
    };

    const view_payload in{.label = "borrowed"};

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<compact_tagged_codec>(compact);
    REQUIRE(enc(as_compact(static_tag<12>{}, in)));

    const std::deque<std::byte> non_contiguous(compact.begin(), compact.end());
    view_payload                out{};
    auto                        dec    = make_decoder<compact_tagged_codec>(non_contiguous);
    auto                        result = dec(as_compact(static_tag<12>{}, out));
    REQUIRE_FALSE(result);
    CHECK(result.error() == status_code::contiguous_view_on_non_contiguous_data);
}

TEST_CASE("compact tagged borrowed views decode from contiguous payload storage") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    {
        const std::string_view in{"borrowed"};
        std::vector<std::byte> compact;
        auto                   enc = make_encoder<compact_tagged_codec>(compact);
        REQUIRE(enc(as_compact(static_tag<16>{}, in)));

        std::string_view out;
        auto             dec = make_decoder<compact_tagged_codec>(compact);
        REQUIRE(dec(as_compact(static_tag<16>{}, out)));
        CHECK(out == in);
        CHECK(out.data() >= reinterpret_cast<const char *>(compact.data()));
        CHECK(out.data() + out.size() <= reinterpret_cast<const char *>(compact.data() + compact.size()));
    }
    {
        const std::vector<std::byte> in{std::byte{0xAA}, std::byte{0xBB}};
        std::span<const std::byte>   in_view{in};
        std::vector<std::byte>       compact;
        auto                         enc = make_encoder<compact_tagged_codec>(compact);
        REQUIRE(enc(as_compact(static_tag<17>{}, in_view)));

        std::span<const std::byte> out;
        auto                       dec = make_decoder<compact_tagged_codec>(compact);
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
        auto                                  enc = make_encoder<compact_tagged_codec>(compact);
        REQUIRE(enc(as_compact(static_tag<13>{}, in)));

        const std::deque<std::byte>        non_contiguous(compact.begin(), compact.end());
        std::optional<std::string_view>    out{};
        auto                               dec    = make_decoder<compact_tagged_codec>(non_contiguous);
        auto                               result = dec(as_compact(static_tag<13>{}, out));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::contiguous_view_on_non_contiguous_data);
    }
    {
        std::array<std::byte, 2> in_bytes{std::byte{0x12}, std::byte{0x34}};
        const compact_non_default_view_payload in{.bytes = std::span<const std::byte, 2>{in_bytes}, .label = "borrowed"};
        std::vector<std::byte> compact;
        auto                   enc = make_encoder<compact_tagged_codec>(compact);
        REQUIRE(enc(as_compact(static_tag<14>{}, in)));

        const std::deque<std::byte> non_contiguous(compact.begin(), compact.end());
        std::array<std::byte, 2>    out_bytes{};
        compact_non_default_view_payload out{.bytes = std::span<const std::byte, 2>{out_bytes}, .label = {}};
        auto                             dec    = make_decoder<compact_tagged_codec>(non_contiguous);
        auto                             result = dec(as_compact(static_tag<14>{}, out));
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::contiguous_view_on_non_contiguous_data);
    }
}

TEST_CASE("compact tagged owning values decode from non-contiguous payload storage") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    const std::vector<std::uint16_t> values{0x1234, 0xABCD};
    std::vector<std::byte>           compact;
    auto                             enc = make_encoder<compact_tagged_codec>(compact);
    REQUIRE(enc(as_compact(static_tag<15>{}, values)));

    const std::deque<std::byte> non_contiguous(compact.begin(), compact.end());
    std::vector<std::uint16_t>  decoded;
    auto                        dec = make_decoder<compact_tagged_codec>(non_contiguous);
    REQUIRE(dec(as_compact(static_tag<15>{}, decoded)));
    CHECK(decoded == values);
}

TEST_CASE("compact tagged non-contiguous containers roundtrip through compact ranges") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    const std::deque<std::uint16_t> values{0x1234, 0xABCD};

    std::vector<std::byte> compact;
    auto                   enc = make_encoder<compact_tagged_codec>(compact);
    REQUIRE(enc(as_compact(static_tag<18>{}, values)));
    CHECK_EQ(to_hex(compact), "d245023412cdab");

    std::deque<std::uint16_t> decoded;
    auto                      dec = make_decoder<compact_tagged_codec>(compact);
    REQUIRE(dec(as_compact(static_tag<18>{}, decoded)));
    CHECK(decoded == values);
}

TEST_CASE("compact tagged fixed borrowed binary spans validate compact length") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    std::array<std::byte, 2>        backing{std::byte{0xCC}, std::byte{0xDD}};
    std::span<const std::byte, 2>   decoded{backing};
    auto                            result = decode_compact_hex("c14201aa", as_compact(static_tag<1>{}, decoded));
    REQUIRE_FALSE(result);
    CHECK(result.error() == status_code::unexpected_group_size);
    CHECK(decoded.data() == backing.data());
}

TEST_CASE("compact tagged truncated payload reports incomplete") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::compact;

    const std::vector<std::uint32_t> values{1, 2, 3};
    std::vector<std::byte>           compact;
    auto                             enc = make_encoder<compact_tagged_codec>(compact);
    REQUIRE(enc(as_compact(static_tag<11>{}, values)));

    compact.pop_back();

    std::vector<std::uint32_t> decoded;
    auto                       dec    = make_decoder<compact_tagged_codec>(compact);
    auto                       result = dec(as_compact(static_tag<11>{}, decoded));
    REQUIRE_FALSE(result);
    CHECK(result.error() == status_code::incomplete);
}
