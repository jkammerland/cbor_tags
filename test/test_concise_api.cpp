#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/codec/typed_array.h>
#include <cbor_tags/extensions/cbor_visualization.h>
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
struct mapped_value {};
// Extension wrappers are non-aggregates, like the built-in RFC 8746 wrappers.
struct byte_array {
    byte_array() = default;
    std::vector<std::byte> payload;
};
struct text_array {
    text_array() = default;
    std::vector<std::string> payload;
};
struct matrix {
    matrix() = default;
    std::vector<std::string> payload;
};

struct value {
    int number{};
};

template <typename Self> struct value_codec : ct::codec::base<Self> {
    using base = ct::codec::base<Self>;
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

template <> struct wire_type<concise_api_test::mapped_value> {
    using type = std::string;
};

template <> struct tagged_bstr_array_traits<concise_api_test::byte_array> {
    static constexpr std::uint64_t tag               = 1001;
    static constexpr std::uint64_t element_byte_size = 2;
};
template <> struct homogeneous_array_traits<concise_api_test::text_array> {
    using array_type                   = std::vector<std::string>;
    static constexpr std::uint64_t tag = 41;
};
template <> struct multi_dimensional_array_traits<concise_api_test::matrix> {
    using dimensions_type              = std::array<std::uint64_t, 2>;
    using array_type                   = std::vector<std::string>;
    static constexpr std::uint64_t tag = 1040;
};

} // namespace cbor::tags::cddl

namespace concise_api_test {

static_assert(!HasWireType<unmapped>);
static_assert(std::is_same_v<cddl::wire_type_t<mapped_value>, std::string>);

TEST_CASE("CDDL traits describe custom wire representations") {
    CHECK(schema<mapped_value>() == "mapped_value = tstr");
    CHECK(schema<std::vector<mapped_value>>() == "root = [* tstr]");
    CHECK(schema<byte_array>() == "root = #6.1001(bstr)");
    CHECK(schema<ct::bounded_size<byte_array, 1, 3>>() == "root = #6.1001(bstr .size (2..6))");
    CHECK(schema<text_array>() == "root = #6.41([* tstr])");
    CHECK(schema<matrix>() == "root = #6.1040([[2*2 uint], [* tstr]])");
    CHECK(schema<ct::rfc8746::typed_array<std::int32_t>>() == "root = #6.78(bstr)");
}

TEST_CASE("CDDL schema supports defaults and explicit options") {
    std::string defaults;
    cddl::schema_to<int>(defaults);
    CHECK(defaults == "root = int");

    fmt::memory_buffer output;
    cddl::schema_to<const mapped_value &>(output, {.row_options = {.format_by_rows = false}, .root_name = "message"});
    CHECK(fmt::to_string(output) == "message = tstr");
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
