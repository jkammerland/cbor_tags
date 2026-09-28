#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/cbor_visualization.h>
#include <cbor_tags/extensions/rfc8746_typed_arrays.h>
#include <cstdint>
#include <doctest/doctest.h>
#include <fmt/format.h>
#include <string>
#include <type_traits>
#include <vector>

namespace concise_api_test {

namespace ct   = cbor::tags;
namespace cddl = ct::cddl;

struct unmapped {};
struct legacy_wire {};
struct new_wire {};
struct both_wire {};
// Extension wrappers are non-aggregates, like the built-in RFC 8746 wrappers.
struct legacy_bytes {
    legacy_bytes() = default;
    std::vector<std::byte> payload;
};
struct new_bytes {
    new_bytes() = default;
    std::vector<std::byte> payload;
};
struct legacy_array {
    legacy_array() = default;
    std::vector<int> payload;
};
struct new_array {
    new_array() = default;
    std::vector<std::string> payload;
};
struct legacy_matrix {
    legacy_matrix() = default;
    std::vector<int> payload;
};
struct new_matrix {
    new_matrix() = default;
    std::vector<std::string> payload;
};

struct value {
    int number{};
};

template <typename Self> struct value_codec : ct::codec_mixin_base<Self> {
    using base = ct::codec_mixin_base<Self>;
    using base::decode;
    using base::encode;

    void            encode(const value &input) { static_cast<Self &>(*this).encode(input.number); }
    ct::status_code decode(value &output, ct::major_type major, std::byte info) {
        return static_cast<Self &>(*this).decode(output.number, major, info);
    }
};

template <typename T>
concept HasWireType = requires { typename cddl::wire_type_t<T>; };

template <typename T> std::string schema() {
    std::string output;
    cddl::schema_to<T>(output, {.row_options = {.format_by_rows = false}});
    return output;
}

} // namespace concise_api_test

namespace cbor::tags::cddl {

template <> struct cddl_wire_type<concise_api_test::legacy_wire> {
    using type = int;
};
template <> struct wire_type<concise_api_test::new_wire> {
    using type = std::string;
};
template <> struct cddl_wire_type<concise_api_test::both_wire> {
    using type = int;
};
template <> struct wire_type<concise_api_test::both_wire> {
    using type = bool;
};

template <> struct cddl_tagged_bstr_array_traits<concise_api_test::legacy_bytes> {
    static constexpr std::uint64_t tag               = 1000;
    static constexpr std::uint64_t element_byte_size = 4;
};
template <> struct tagged_bstr_array_traits<concise_api_test::new_bytes> {
    static constexpr std::uint64_t tag               = 1001;
    static constexpr std::uint64_t element_byte_size = 2;
};
template <> struct cddl_homogeneous_array_traits<concise_api_test::legacy_array> {
    using array_type                   = std::vector<int>;
    static constexpr std::uint64_t tag = 41;
};
template <> struct homogeneous_array_traits<concise_api_test::new_array> {
    using array_type                   = std::vector<std::string>;
    static constexpr std::uint64_t tag = 41;
};
template <> struct cddl_multi_dimensional_array_traits<concise_api_test::legacy_matrix> {
    using dimensions_type              = std::array<std::uint64_t, 2>;
    using array_type                   = std::vector<int>;
    static constexpr std::uint64_t tag = 40;
};
template <> struct multi_dimensional_array_traits<concise_api_test::new_matrix> {
    using dimensions_type              = std::array<std::uint64_t, 2>;
    using array_type                   = std::vector<std::string>;
    static constexpr std::uint64_t tag = 1040;
};

} // namespace cbor::tags::cddl

namespace concise_api_test {

static_assert(!HasWireType<unmapped>);
static_assert(std::is_same_v<cddl::wire_type_t<legacy_wire>, int>);
static_assert(std::is_same_v<cddl::wire_type_t<new_wire>, std::string>);
static_assert(std::is_same_v<cddl::wire_type_t<both_wire>, bool>);
static_assert(std::is_same_v<cddl::options, ct::CDDLOptions>);
static_assert(std::is_same_v<cddl::enum_mode, ct::CDDLEnumMode>);
static_assert(std::is_same_v<ct::encoder_mixin_base<value>, ct::cbor_encoder_mixin_base<value>>);
static_assert(std::is_same_v<ct::decoder_mixin_base<value>, ct::cbor_decoder_mixin_base<value>>);
static_assert(std::is_same_v<ct::codec_mixin_base<value>, ct::cbor_codec_mixin_base<value>>);

TEST_CASE("Concise CDDL traits preserve legacy mappings and prefer explicit new mappings") {
    CHECK(schema<legacy_wire>() == "legacy_wire = int");
    CHECK(schema<new_wire>() == "new_wire = tstr");
    CHECK(schema<both_wire>() == "both_wire = bool");
    CHECK(schema<std::vector<new_wire>>() == "root = [* tstr]");
    CHECK(schema<legacy_bytes>() == "root = #6.1000(bstr)");
    CHECK(schema<new_bytes>() == "root = #6.1001(bstr)");
    CHECK(schema<ct::bounded_size<new_bytes, 1, 3>>() == "root = #6.1001(bstr .size (2..6))");
    CHECK(schema<legacy_array>() == "root = #6.41([* int])");
    CHECK(schema<new_array>() == "root = #6.41([* tstr])");
    CHECK(schema<legacy_matrix>() == "root = #6.40([[2*2 uint], [* int]])");
    CHECK(schema<new_matrix>() == "root = #6.1040([[2*2 uint], [* tstr]])");
    CHECK(schema<ct::ext::rfc8746::typed_array<std::int32_t>>() == "root = #6.78(bstr)");
}

TEST_CASE("Concise schema function supports defaults, explicit options and legacy callers") {
    std::string defaults;
    cddl::schema_to<int>(defaults);
    CHECK(defaults == "root = int");

    fmt::memory_buffer output;
    cddl::schema_to<const new_wire &>(output, {.row_options = {.format_by_rows = false}, .root_name = "message"});
    CHECK(fmt::to_string(output) == "message = tstr");

    std::string legacy;
    ct::cddl_schema_to<new_wire>(legacy, {.row_options = {.format_by_rows = false}});
    CHECK(legacy == "new_wire = tstr");
}

TEST_CASE("Concise codec base composes scalar overloads with application overloads") {
    std::vector<std::byte> bytes;
    REQUIRE(ct::make_encoder<value_codec>(bytes)(value{42}, 7));
    CHECK(bytes == std::vector<std::byte>{std::byte{0x18}, std::byte{0x2a}, std::byte{0x07}});
    value decoded;
    int   scalar{};
    REQUIRE(ct::make_decoder<value_codec>(bytes)(decoded, scalar));
    CHECK(decoded.number == 42);
    CHECK(scalar == 7);
}

} // namespace concise_api_test
