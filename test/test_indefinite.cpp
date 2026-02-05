#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

#include <cbor_tags/cbor_concepts.h>
#include <cbor_tags/cbor_concepts_checking.h>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <doctest/doctest.h>
#include "test_util.h"

using namespace cbor::tags;

static_assert(IsArray<as_indefinite<std::vector<int>>>);
static_assert(IsMap<as_indefinite<std::map<int, int>>>);
static_assert(IsTextString<as_indefinite<std::string>>);
static_assert(IsArray<as_maybe_indefinite<std::vector<int>>>);
static_assert(IsMap<as_maybe_indefinite<std::map<int, int>>>);
static_assert(IsTextString<as_maybe_indefinite<std::string>>);
static_assert(IsMap<const std::map<int, int>>);
static_assert(!IsArray<const std::map<int, int>>);

struct IndefTagged {
    std::vector<int>           values;
    std::map<int, std::string> labels;
    bool operator==(const IndefTagged &) const = default;

  private:
    friend cbor::tags::Access;

    template <typename Encoder> constexpr auto encode(Encoder &enc) const {
        return enc(as_array{2}, as_indefinite{values}, as_indefinite{labels});
    }
    template <typename Decoder> constexpr auto decode(Decoder &dec) {
        return dec(as_array{2}, as_indefinite{values}, as_indefinite{labels});
    }
};

namespace cbor::tags {
template <> constexpr auto cbor_tag<::IndefTagged>() { return static_tag<700>{}; }
} // namespace cbor::tags

static_assert(IsCborMajor<IndefTagged>);
static_assert(IsCborMajor<std::variant<int, IndefTagged>>);
static_assert(is_valid_major<major_type, IndefTagged>(major_type::Tag));
static_assert(valid_concept_mapping_v<std::variant<int, IndefTagged, std::string>>);

struct IndefNested {
    int                    id{};
    std::vector<int>       values;
    std::map<int, IndefTagged> tagged;
    bool operator==(const IndefNested &) const = default;

  private:
    friend cbor::tags::Access;
    template <typename Encoder> constexpr auto encode(Encoder &enc) const { return enc(as_array{3}, id, as_indefinite{values}, tagged); }
    template <typename Decoder> constexpr auto decode(Decoder &dec) { return dec(as_array{3}, id, as_indefinite{values}, tagged); }
};

struct IndefEmbedded {
    int                              id{};
    std::variant<int, IndefTagged>   payload;
    std::vector<std::byte>           blob;
    std::string                      label;
    bool operator==(const IndefEmbedded &) const = default;

  private:
    friend cbor::tags::Access;
    template <typename Encoder> constexpr auto encode(Encoder &enc) const {
        return enc(as_array{4}, id, payload, as_indefinite{blob}, as_indefinite{label});
    }
    template <typename Decoder> constexpr auto decode(Decoder &dec) {
        return dec(as_array{4}, id, payload, as_indefinite{blob}, as_indefinite{label});
    }
};

TEST_CASE("decode indefinite bstr into vector") {
    std::vector<std::byte> buffer{std::byte{0x5F}, std::byte{0x42}, std::byte{0x01}, std::byte{0x02}, std::byte{0x41},
                                  std::byte{0x03}, std::byte{0xFF}};

    auto dec = make_decoder(buffer);

    std::vector<std::byte> decoded;
    auto                   result = dec(as_indefinite{decoded});

    CHECK_MESSAGE(result, "Decoding an indefinite byte string should succeed.");
    CHECK_EQ(decoded.size(), 3);
    CHECK_EQ(decoded[0], std::byte{0x01});
    CHECK_EQ(decoded[1], std::byte{0x02});
    CHECK_EQ(decoded[2], std::byte{0x03});
}

TEST_CASE("decode maybe indefinite bstr (definite)") {
    std::vector<std::byte> buffer{std::byte{0x43}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    auto dec = make_decoder(buffer);

    std::vector<std::byte> decoded;
    auto                   result = dec(as_maybe_indefinite{decoded});

    CHECK_MESSAGE(result, "Definite bstr should decode via maybe indefinite.");
    CHECK_EQ(decoded.size(), 3);
    CHECK_EQ(decoded[0], std::byte{0x01});
    CHECK_EQ(decoded[1], std::byte{0x02});
    CHECK_EQ(decoded[2], std::byte{0x03});
}

TEST_CASE("decode maybe indefinite bstr (indefinite)") {
    std::vector<std::byte> buffer{std::byte{0x5F}, std::byte{0x41}, std::byte{0xAA}, std::byte{0xFF}};

    auto dec = make_decoder(buffer);

    std::vector<std::byte> decoded;
    auto                   result = dec(as_maybe_indefinite{decoded});

    CHECK_MESSAGE(result, "Indefinite bstr should decode via maybe indefinite.");
    CHECK_EQ(decoded.size(), 1);
    CHECK_EQ(decoded[0], std::byte{0xAA});
}

TEST_CASE("decode indefinite bstr with wrong chunk type") {
    std::vector<std::byte> buffer{std::byte{0x5F}, std::byte{0x60}, std::byte{0xFF}};

    auto dec = make_decoder(buffer);

    std::vector<std::byte> decoded;
    auto                   result = dec(as_indefinite{decoded});

    CHECK_FALSE_MESSAGE(result, "Wrong chunk major type should fail decoding indefinite bstr.");
    CHECK_EQ(result.error(), status_code::no_match_for_bstr_on_buffer);
}

TEST_CASE("decode indefinite tstr into string") {
    std::vector<std::byte> buffer{std::byte{0x7F}, std::byte{0x62}, std::byte{0x61}, std::byte{0x62}, std::byte{0x61},
                                  std::byte{0x63}, std::byte{0xFF}};

    auto dec = make_decoder(buffer);

    std::string decoded;
    auto        result = dec(as_indefinite{decoded});

    CHECK_MESSAGE(result, "Decoding an indefinite text string should succeed.");
    CHECK_EQ(decoded, "abc");
}

TEST_CASE("decode maybe indefinite tstr (definite)") {
    std::vector<std::byte> buffer{std::byte{0x62}, std::byte{'h'}, std::byte{'i'}};

    auto dec = make_decoder(buffer);

    std::string decoded;
    auto        result = dec(as_maybe_indefinite{decoded});

    CHECK_MESSAGE(result, "Definite tstr should decode via maybe indefinite.");
    CHECK_EQ(decoded, "hi");
}

TEST_CASE("decode maybe indefinite tstr (indefinite)") {
    std::vector<std::byte> buffer{std::byte{0x7F}, std::byte{0x61}, std::byte{'h'}, std::byte{0x61}, std::byte{'i'},
                                  std::byte{0xFF}};

    auto dec = make_decoder(buffer);

    std::string decoded;
    auto        result = dec(as_maybe_indefinite{decoded});

    CHECK_MESSAGE(result, "Indefinite tstr should decode via maybe indefinite.");
    CHECK_EQ(decoded, "hi");
}

TEST_CASE("decode indefinite tstr with wrong chunk type") {
    std::vector<std::byte> buffer{std::byte{0x7F}, std::byte{0x40}, std::byte{0xFF}};

    auto dec = make_decoder(buffer);

    std::string decoded;
    auto        result = dec(as_indefinite{decoded});

    CHECK_FALSE_MESSAGE(result, "Wrong chunk major type should fail decoding indefinite tstr.");
    CHECK_EQ(result.error(), status_code::no_match_for_tstr_on_buffer);
}

TEST_CASE("decode indefinite array into vector") {
    std::vector<std::byte> buffer{std::byte{0x9F}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0xFF}};

    auto dec = make_decoder(buffer);

    std::vector<int> decoded;
    auto             result = dec(as_indefinite{decoded});

    CHECK_MESSAGE(result, "Decoding an indefinite array should succeed.");
    CHECK_EQ(decoded.size(), 3);
    CHECK_EQ(decoded[0], 1);
    CHECK_EQ(decoded[1], 2);
    CHECK_EQ(decoded[2], 3);
}

TEST_CASE("decode maybe indefinite array (definite)") {
    std::vector<std::byte> buffer{std::byte{0x82}, std::byte{0x01}, std::byte{0x02}};

    auto dec = make_decoder(buffer);

    std::vector<int> decoded;
    auto             result = dec(as_maybe_indefinite{decoded});

    CHECK_MESSAGE(result, "Definite array should decode via maybe indefinite.");
    CHECK_EQ(decoded.size(), 2);
    CHECK_EQ(decoded[0], 1);
    CHECK_EQ(decoded[1], 2);
}

TEST_CASE("decode maybe indefinite array (indefinite)") {
    std::vector<std::byte> buffer{std::byte{0x9F}, std::byte{0x01}, std::byte{0x02}, std::byte{0xFF}};

    auto dec = make_decoder(buffer);

    std::vector<int> decoded;
    auto             result = dec(as_maybe_indefinite{decoded});

    CHECK_MESSAGE(result, "Indefinite array should decode via maybe indefinite.");
    CHECK_EQ(decoded.size(), 2);
    CHECK_EQ(decoded[0], 1);
    CHECK_EQ(decoded[1], 2);
}

TEST_CASE("decode indefinite array without break returns incomplete") {
    std::vector<std::byte> buffer{std::byte{0x9F}, std::byte{0x01}, std::byte{0x02}};

    auto dec = make_decoder(buffer);

    std::vector<int> decoded;
    auto             result = dec(as_indefinite{decoded});

    CHECK_FALSE_MESSAGE(result, "Missing break marker should report incomplete.");
    CHECK_EQ(result.error(), status_code::incomplete);
    CHECK_EQ(decoded.size(), 2);
    CHECK_EQ(decoded[0], 1);
    CHECK_EQ(decoded[1], 2);
}

TEST_CASE("decode indefinite map into map") {
    std::vector<std::byte> buffer{std::byte{0xBF}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
                                  std::byte{0xFF}};

    auto dec = make_decoder(buffer);

    std::map<int, int> decoded;
    auto               result = dec(as_indefinite{decoded});

    CHECK_MESSAGE(result, "Decoding an indefinite map should succeed.");
    CHECK_EQ(decoded.size(), 2);
    CHECK_EQ(decoded[1], 2);
    CHECK_EQ(decoded[3], 4);
}

TEST_CASE("decode maybe indefinite map (definite)") {
    std::vector<std::byte> buffer{std::byte{0xA1}, std::byte{0x01}, std::byte{0x02}};

    auto dec = make_decoder(buffer);

    std::map<int, int> decoded;
    auto               result = dec(as_maybe_indefinite{decoded});

    CHECK_MESSAGE(result, "Definite map should decode via maybe indefinite.");
    CHECK_EQ(decoded.size(), 1);
    CHECK_EQ(decoded[1], 2);
}

TEST_CASE("decode maybe indefinite map (indefinite)") {
    std::vector<std::byte> buffer{std::byte{0xBF}, std::byte{0x01}, std::byte{0x02}, std::byte{0xFF}};

    auto dec = make_decoder(buffer);

    std::map<int, int> decoded;
    auto               result = dec(as_maybe_indefinite{decoded});

    CHECK_MESSAGE(result, "Indefinite map should decode via maybe indefinite.");
    CHECK_EQ(decoded.size(), 1);
    CHECK_EQ(decoded[1], 2);
}

TEST_CASE("decode indefinite map without break returns incomplete") {
    std::vector<std::byte> buffer{std::byte{0xBF}, std::byte{0x01}, std::byte{0x02}};

    auto dec = make_decoder(buffer);

    std::map<int, int> decoded;
    auto               result = dec(as_indefinite{decoded});

    CHECK_FALSE_MESSAGE(result, "Missing break marker should report incomplete.");
    CHECK_EQ(result.error(), status_code::incomplete);
    CHECK_EQ(decoded.size(), 1);
    CHECK_EQ(decoded[1], 2);
}

TEST_CASE("decode indefinite bstr without break returns incomplete") {
    std::vector<std::byte> buffer{std::byte{0x5F}, std::byte{0x41}, std::byte{0xAA}};

    auto dec = make_decoder(buffer);

    std::vector<std::byte> decoded;
    auto                   result = dec(as_indefinite{decoded});

    CHECK_FALSE_MESSAGE(result, "Missing break marker should report incomplete.");
    CHECK_EQ(result.error(), status_code::incomplete);
    CHECK_EQ(decoded.size(), 1);
    CHECK_EQ(decoded[0], std::byte{0xAA});
}

TEST_CASE("decode indefinite bstr with indefinite chunk returns no match") {
    std::vector<std::byte> buffer{std::byte{0x5F}, std::byte{0x5F}, std::byte{0xFF}};

    auto dec = make_decoder(buffer);

    std::vector<std::byte> decoded;
    auto                   result = dec(as_indefinite{decoded});

    CHECK_FALSE_MESSAGE(result, "Nested indefinite bstr chunks are not allowed.");
    CHECK_EQ(result.error(), status_code::no_match_for_bstr_on_buffer);
}

TEST_CASE("decode indefinite bstr with truncated chunk returns incomplete") {
    std::vector<std::byte> buffer{std::byte{0x5F}, std::byte{0x42}, std::byte{0xAA}};

    auto dec = make_decoder(buffer);

    std::vector<std::byte> decoded;
    auto                   result = dec(as_indefinite{decoded});

    CHECK_FALSE_MESSAGE(result, "Truncated chunk payload should report incomplete.");
    CHECK_EQ(result.error(), status_code::incomplete);
    CHECK(decoded.empty());
}

TEST_CASE("encode maybe indefinite always definite") {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);

    std::vector<std::byte> bytes{std::byte{0x01}, std::byte{0x02}};
    REQUIRE(enc(as_maybe_indefinite{bytes}));
    CHECK_EQ(to_hex(buffer).substr(0, 2), "42");

    buffer.clear();
    std::string text = "hi";
    REQUIRE(enc(as_maybe_indefinite{text}));
    CHECK_EQ(to_hex(buffer).substr(0, 2), "62");

    buffer.clear();
    std::vector<int> array{1, 2};
    REQUIRE(enc(as_maybe_indefinite{array}));
    CHECK_EQ(to_hex(buffer).substr(0, 2), "82");

    buffer.clear();
    std::map<int, int> map{{1, 2}};
    REQUIRE(enc(as_maybe_indefinite{map}));
    CHECK_EQ(to_hex(buffer).substr(0, 2), "a1");
}

TEST_CASE("roundtrip indefinite tagged class direct") {
    IndefTagged input{.values = {1, 2, 3}, .labels = {{1, "one"}, {2, "two"}}};

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(input));

    auto hex = to_hex(buffer);
    CBOR_TAGS_TEST_LOG("indef tagged hex: {}\n", hex);
    CHECK_EQ(hex.substr(0, 6), "d902bc");

    auto        dec = make_decoder(buffer);
    IndefTagged decoded;
    auto        result = dec(decoded);

    CHECK_MESSAGE(result, "Decoding tagged class directly should succeed.");
    CHECK_EQ(decoded, input);
}

TEST_CASE("decode tagged class pieces") {
    IndefTagged input{.values = {1, 2, 3}, .labels = {{1, "one"}, {2, "two"}}};

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(input));

    auto hex = to_hex(buffer);
    CBOR_TAGS_TEST_LOG("indef tagged hex: {}\n", hex);

    auto dec = make_decoder(buffer);

    auto tag_status = dec.decode(static_tag<700>{});
    CHECK_EQ(tag_status, status_code::success);

    auto array_status = dec.decode(as_array{2});
    CHECK_EQ(array_status, status_code::success);

    std::vector<int> values;
    auto             values_status = dec.decode(as_indefinite{values});
    CHECK_EQ(values_status, status_code::success);
    CHECK_EQ(values, input.values);

    std::map<int, std::string> labels;
    auto                       labels_status = dec.decode(as_indefinite{labels});
    CHECK_EQ(labels_status, status_code::success);
    CHECK_EQ(labels, input.labels);
}

TEST_CASE("decode tagged class after explicit tag") {
    IndefTagged input{.values = {4, 5}, .labels = {{1, "x"}}};

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(input));

    auto dec = make_decoder(buffer);

    auto tag_status = dec.decode(static_tag<700>{});
    CHECK_EQ(tag_status, status_code::success);

    IndefTagged decoded;
    auto        status = dec.decode_without_tag(decoded);

    CHECK_EQ(status, status_code::success);
    CHECK_EQ(decoded, input);
}

TEST_CASE("roundtrip indefinite tagged class in variant") {
    IndefTagged input{.values = {1, 2, 3}, .labels = {{1, "one"}, {2, "two"}}};

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(input));

    auto                                 dec = make_decoder(buffer);
    std::variant<int, IndefTagged, std::string> v;
    auto                                 result = dec(v);

    CHECK_MESSAGE(result, "Decoding tagged class via variant should succeed.");
    CHECK(std::holds_alternative<IndefTagged>(v));
    CHECK_EQ(std::get<IndefTagged>(v), input);
}

TEST_CASE("roundtrip variant with tstr over tagged class") {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(std::string{"hello"}));

    auto                                 dec = make_decoder(buffer);
    std::variant<int, IndefTagged, std::string> v;
    auto                                 result = dec(v);

    CHECK_MESSAGE(result, "Variant should select string when tstr is present.");
    CHECK(std::holds_alternative<std::string>(v));
    CHECK_EQ(std::get<std::string>(v), "hello");
}

TEST_CASE("roundtrip nested indefinite structures") {
    IndefNested input{.id = 7,
                      .values = {10, 20},
                      .tagged = {{1, IndefTagged{.values = {1, 2}, .labels = {{1, "a"}}}},
                                 {2, IndefTagged{.values = {3, 4}, .labels = {{2, "b"}, {3, "c"}}}}}};

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(input));

    auto       dec = make_decoder(buffer);
    IndefNested decoded;
    auto       result = dec(decoded);

    CHECK_MESSAGE(result, "Nested indefinite structures should roundtrip.");
    CHECK_EQ(decoded, input);
}

TEST_CASE("roundtrip embedded indefinite fields in structs") {
    IndefEmbedded input{.id = 42,
                        .payload = IndefTagged{.values = {9}, .labels = {{1, "nine"}}},
                        .blob = {std::byte{0xAA}, std::byte{0xBB}},
                        .label = "ok"};

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(input));

    auto         dec = make_decoder(buffer);
    IndefEmbedded decoded;
    auto         result = dec(decoded);

    CHECK_MESSAGE(result, "Embedded indefinite fields should roundtrip.");
    CHECK_EQ(decoded, input);
}
