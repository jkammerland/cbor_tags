#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/rfc8746_typed_arrays.h"
#include "cbor_tags/float16_ieee754.h"

#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <glaze/cbor.hpp>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cbor_tags_interop_glaze {

struct record {
    std::string               name;
    std::uint64_t             age{};
    bool                      active{};
    std::vector<std::int32_t> samples;

    bool operator==(const record &) const = default;
};

} // namespace cbor_tags_interop_glaze

template <> struct glz::meta<cbor_tags_interop_glaze::record> {
    using T                     = cbor_tags_interop_glaze::record;
    static constexpr auto value = object("name", &T::name, "age", &T::age, "active", &T::active, "samples", &T::samples);
};

namespace cbor_tags_interop_glaze {

[[nodiscard]] std::string as_string(const std::vector<std::byte> &bytes) {
    auto result = std::string{};
    result.reserve(bytes.size());
    for (const auto byte : bytes) {
        result.push_back(static_cast<char>(byte));
    }
    return result;
}

[[nodiscard]] std::vector<std::byte> as_bytes(std::string_view bytes) {
    auto result = std::vector<std::byte>{};
    result.reserve(bytes.size());
    for (const auto byte : bytes) {
        result.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
    }
    return result;
}

template <typename... Args> [[nodiscard]] std::vector<std::byte> encode_with_cbor_tags(Args &&...args) {
    auto output = std::vector<std::byte>{};
    auto enc    = cbor::tags::make_encoder<cbor::tags::ext::rfc8746::typed_array_codec>(output);
    auto result = enc(std::forward<Args>(args)...);
    REQUIRE(result);
    return output;
}

template <typename T> [[nodiscard]] std::vector<std::byte> encode_with_glaze(const T &value) {
    auto output = std::string{};
    auto result = glz::write_cbor(value, output);
    REQUIRE(!result);
    return as_bytes(output);
}

} // namespace cbor_tags_interop_glaze

TEST_CASE("glaze reads text-key maps emitted by cbor_tags") {
    using namespace cbor::tags;
    using namespace cbor_tags_interop_glaze;
    using namespace std::string_view_literals;

    const auto output = encode_with_cbor_tags(as_map{4}, "name"sv, "Ada"sv, "age"sv, 42U, "active"sv, true, "samples"sv,
                                              std::vector<std::int32_t>{1, -2, 3});

    auto decoded = record{};
    auto result  = glz::read_cbor(decoded, as_string(output));

    REQUIRE(!result);
    CHECK(decoded == record{"Ada", 42U, true, {1, -2, 3}});
}

TEST_CASE("cbor_tags reads Glaze text-key maps and typed-array fields") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::rfc8746;
    using namespace cbor_tags_interop_glaze;

    const auto input = encode_with_glaze(record{"Ada", 42U, true, {1, -2, 3}});

    auto dec = make_decoder<typed_array_codec>(input);
    REQUIRE(dec(as_map{4}));

    auto key  = std::string{};
    auto name = std::string{};
    REQUIRE(dec(key, name));
    CHECK_EQ(key, "name");
    CHECK_EQ(name, "Ada");

    auto age = std::uint64_t{};
    REQUIRE(dec(key, age));
    CHECK_EQ(key, "age");
    CHECK_EQ(age, 42U);

    auto active = false;
    REQUIRE(dec(key, active));
    CHECK_EQ(key, "active");
    CHECK(active);

    auto samples = typed_array<std::int32_t>{};
    REQUIRE(dec(key, samples));
    CHECK_EQ(key, "samples");
    CHECK_EQ(samples.values(), std::vector<std::int32_t>{1, -2, 3});
}

TEST_CASE("Glaze numeric vectors use RFC 8746 typed arrays") {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::rfc8746;
    using namespace cbor_tags_interop_glaze;

    const auto values      = std::vector<std::int32_t>{1, -2, 3};
    const auto glaze_bytes = encode_with_glaze(values);

    auto generic_values = std::vector<std::int32_t>{};
    auto generic_dec    = make_decoder(glaze_bytes);
    CHECK_FALSE(generic_dec(generic_values));

    auto typed_values = typed_array<std::int32_t>{};
    auto typed_dec    = make_decoder<typed_array_codec>(glaze_bytes);
    REQUIRE(typed_dec(typed_values));
    CHECK_EQ(typed_values.values(), values);

    const auto cbor_tags_bytes = encode_with_cbor_tags(as_typed_array(values));
    auto       glaze_values    = std::vector<std::int32_t>{};
    auto       result          = glz::read_cbor(glaze_values, as_string(cbor_tags_bytes));
    REQUIRE(!result);
    CHECK_EQ(glaze_values, values);
}

TEST_CASE("Glaze and cbor_tags agree on non-text map keys when the target type is homogeneous") {
    using namespace cbor::tags;
    using namespace cbor_tags_interop_glaze;
    using namespace std::string_view_literals;

    const auto cbor_tags_bytes = encode_with_cbor_tags(as_map{2}, 1, "one"sv, 2, "two"sv);
    auto       glaze_map       = std::map<int, std::string>{};
    auto       glaze_result    = glz::read_cbor(glaze_map, as_string(cbor_tags_bytes));

    REQUIRE(!glaze_result);
    CHECK(glaze_map == std::map<int, std::string>{{1, "one"}, {2, "two"}});

    const auto glaze_bytes = encode_with_glaze(std::map<int, double>{{1, 3.141592653589793}, {2, 2.718281828459045}});
    auto       cbor_map    = std::map<int, double>{};
    auto       dec         = make_decoder(glaze_bytes);
    REQUIRE(dec(cbor_map));
    CHECK(cbor_map == std::map<int, double>{{1, 3.141592653589793}, {2, 2.718281828459045}});
}

TEST_CASE("Glaze accepts valid indefinite arrays maps text and byte strings") {
    using namespace cbor_tags_interop_glaze;

    auto values = std::vector<int>{};
    REQUIRE(!glz::read_cbor(values, std::string{"\x9f\x01\x02\xff", 4}));
    CHECK_EQ(values, std::vector<int>{1, 2});

    auto map = std::map<std::string, int>{};
    REQUIRE(!glz::read_cbor(map, std::string{"\xbf\x61"
                                             "a\x01\xff",
                                             5}));
    CHECK(map == std::map<std::string, int>{{"a", 1}});

    auto text = std::string{};
    REQUIRE(!glz::read_cbor(text, std::string{"\x7f\x61"
                                              "A\x61"
                                              "B\xff",
                                              7}));
    CHECK_EQ(text, "AB");

    auto bytes = std::vector<std::uint8_t>{};
    REQUIRE(!glz::read_cbor(bytes, std::string{"\x5f\x41\x01\x41\x02\xff", 6}));
    CHECK_EQ(bytes, std::vector<std::uint8_t>{1, 2});
}

TEST_CASE("Glaze and cbor_tags agree on finite floats") {
    using namespace cbor::tags;
    using namespace cbor_tags_interop_glaze;

    auto half = double{};
    REQUIRE(!glz::read_cbor(half, as_string(encode_with_cbor_tags(float16_t{1.5F}))));
    CHECK_EQ(half, doctest::Approx(1.5));

    const auto glaze_bytes = encode_with_glaze(3.141592653589793);
    auto       decoded     = double{};
    auto       dec         = make_decoder(glaze_bytes);
    REQUIRE(dec(decoded));
    CHECK_EQ(decoded, doctest::Approx(3.141592653589793));
}

TEST_CASE("Glaze compact float widths require matching cbor_tags float targets") {
    using namespace cbor::tags;
    using namespace cbor_tags_interop_glaze;

    const auto compact_half = encode_with_glaze(1.5);

    auto widened = double{};
    auto dec     = make_decoder(compact_half);
    CHECK_FALSE(dec(widened));

    auto decoded_half = float16_t{};
    auto half_dec     = make_decoder(compact_half);
    REQUIRE(half_dec(decoded_half));
    CHECK_EQ(static_cast<double>(decoded_half), doctest::Approx(1.5));
}

TEST_CASE("Glaze typed targets reject generic CBOR tags and simple undefined") {
    using namespace cbor::tags;
    using namespace cbor_tags_interop_glaze;
    using namespace std::string_view_literals;

    const auto tagged_text = encode_with_cbor_tags(make_tag_pair(static_tag<42>{}, "Ada"sv));
    auto       text        = std::string{};
    CHECK(glz::read_cbor(text, as_string(tagged_text)));

    auto cbor_tagged_text = make_tag_pair(static_tag<42>{}, std::string{});
    auto dec              = make_decoder(tagged_text);
    REQUIRE(dec(cbor_tagged_text));
    CHECK_EQ(cbor_tagged_text.second, "Ada");

    const auto undefined = encode_with_cbor_tags(simple{23});
    auto       maybe     = std::optional<int>{7};
    CHECK(glz::read_cbor(maybe, as_string(undefined)));

    auto decoded_simple = simple{};
    auto simple_dec     = make_decoder(undefined);
    REQUIRE(simple_dec(decoded_simple));
    CHECK_EQ(decoded_simple, simple{23});
}

TEST_CASE("Glaze collapses duplicate map keys when decoded into std::map") {
    auto decoded = std::map<std::string, int>{};
    auto result  = glz::read_cbor(decoded, std::string{"\xa2\x61"
                                                       "x\x01\x61"
                                                       "x\x02",
                                                      7});

    REQUIRE(!result);
    REQUIRE_EQ(decoded.size(), 1U);
    CHECK_EQ(decoded.at("x"), 2);
}
