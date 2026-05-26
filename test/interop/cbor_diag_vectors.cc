#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/float16_ieee754.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <ios>
#include <iostream>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] std::string to_hex(std::span<const std::byte> bytes) {
    static constexpr auto digits = std::string_view{"0123456789abcdef"};

    auto output = std::string{};
    output.reserve(bytes.size() * 2U);
    for (const auto byte : bytes) {
        const auto value = std::to_integer<unsigned int>(byte);
        output.push_back(digits[(value >> 4U) & 0x0FU]);
        output.push_back(digits[value & 0x0FU]);
    }
    return output;
}

template <typename... Args> [[nodiscard]] std::vector<std::byte> encode(Args &&...args) {
    auto output = std::vector<std::byte>{};
    auto enc    = cbor::tags::make_encoder(output);
    auto result = enc(std::forward<Args>(args)...);
    if (!result) {
        throw std::runtime_error("cbor_tags vector encoding failed");
    }
    return output;
}

void write_case(std::ofstream &output, std::string_view name, std::span<const std::byte> bytes, std::string_view expected_diag) {
    output << name << '\t' << to_hex(bytes) << '\t' << expected_diag << '\n';
}

void write_malformed_case(std::ofstream &output, std::string_view name, std::string_view hex) {
    output << name << '\t' << hex << "\tERROR\n";
}

void write_vectors(std::ofstream &output) {
    using namespace cbor::tags;
    using namespace std::string_view_literals;

    write_case(output, "unsigned_boundaries", encode(wrap_as_array{0U, 1U, 23U, 24U, 255U, 256U, 65535U, 65536U}), "-");
    write_case(output, "signed_boundaries", encode(wrap_as_array{0, -1, -24, -25, -256, -257}), "-");
    write_case(output, "text_strings", encode(wrap_as_array{"IETF"sv, ""sv, "Hello world!"sv}), "-");

    write_case(output, "byte_strings",
               encode(wrap_as_array{std::array<std::byte, 3>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}},
                                    std::array<std::byte, 1>{std::byte{0x00}}}),
               "-");

    write_case(output, "json_shaped_map",
               encode(as_map{4}, "active"sv, true, "age"sv, 42U, "name"sv, "Ada"sv, "scores"sv, std::vector<std::uint64_t>{1, 2, 3}), "-");

    write_case(output, "tagged_byte_string",
               encode(make_tag_pair(static_tag<42>{}, std::array<std::byte, 2>{std::byte{0xca}, std::byte{0xfe}})), "-");

    write_case(output, "nullable_array", encode(wrap_as_array{true, false, nullptr}), "-");
    write_case(output, "uint64_max", encode(std::numeric_limits<std::uint64_t>::max()), "-");
    write_case(output, "float_values",
               encode(wrap_as_array{float16_t{1.5F}, 3.25F, -0.0, std::numeric_limits<double>::infinity(),
                                    -std::numeric_limits<double>::infinity()}),
               "-");
    write_case(output, "simple_values", encode(wrap_as_array{simple{16}, simple{23}, simple{32}, simple{255}}), "-");
    write_case(output, "duplicate_text_keys", encode(as_map{2}, "x"sv, 1U, "x"sv, 2U), "-");

    write_case(output, "tag_boundaries",
               encode(as_array{7}, make_tag_pair(static_tag<0>{}, "1970-01-01T00:00:00Z"sv), make_tag_pair(static_tag<1>{}, 1U),
                      make_tag_pair(static_tag<23>{}, 0U), make_tag_pair(static_tag<24>{}, std::array<std::byte, 1>{std::byte{0xf6}}),
                      make_tag_pair(static_tag<255>{}, "tag255"sv), make_tag_pair(static_tag<256>{}, "tag256"sv),
                      make_tag_pair(static_tag<55799>{}, "self-described payload"sv)),
               "-");
    write_case(output, "nested_tags",
               encode(make_tag_pair(static_tag<55799>{}, make_tag_pair(static_tag<24>{}, std::array<std::byte, 1>{std::byte{0xf6}}))), "-");

    const auto byte_key = std::array<std::byte, 2>{std::byte{0xca}, std::byte{0xfe}};
    write_case(output, "non_text_map_keys",
               encode(as_map{7}, 1U, "uint"sv, negative{1}, "negative"sv, true, "bool"sv, nullptr, "null"sv, byte_key, "bytes"sv,
                      as_array{2}, 1U, 2U, "array"sv, make_tag_pair(static_tag<42>{}, 1U), "tag"sv),
               "-");

    auto indefinite_array = std::vector<std::uint64_t>{1U, 2U, 3U};
    write_case(output, "indefinite_array", encode(as_indefinite{indefinite_array}), "-");

    auto indefinite_map = std::map<std::string, std::uint64_t>{{"a", 1U}, {"b", 2U}};
    write_case(output, "indefinite_map", encode(as_indefinite{indefinite_map}), "-");

    auto indefinite_text = std::string{"Ada"};
    write_case(output, "indefinite_text_string", encode(as_indefinite{indefinite_text}), "-");

    auto indefinite_bytes = std::vector<std::byte>{std::byte{0xca}, std::byte{0xfe}};
    write_case(output, "indefinite_byte_string", encode(as_indefinite{indefinite_bytes}), "-");

    auto nested_indefinite_array = std::vector<std::vector<std::uint64_t>>{{1U, 2U}, {3U}};
    write_case(output, "nested_indefinite_array", encode(as_indefinite{nested_indefinite_array}), "-");

    write_malformed_case(output, "malformed_truncated_uint8", "18");
    write_malformed_case(output, "malformed_short_text_string", "6241");
    write_malformed_case(output, "malformed_unclosed_indefinite_array", "9f0102");
    write_malformed_case(output, "malformed_unclosed_indefinite_map", "bf616101");
    write_malformed_case(output, "malformed_unclosed_indefinite_text_string", "7f6141");
    write_malformed_case(output, "malformed_unclosed_indefinite_byte_string", "5f4101");
    write_malformed_case(output, "malformed_missing_tag_payload", "d82a");
    write_malformed_case(output, "malformed_missing_map_value", "a16161");
    write_malformed_case(output, "malformed_truncated_float64", "fb3ff800");
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <output.tsv>\n";
        return 2;
    }

    try {
        auto output = std::ofstream{argv[1], std::ios::trunc};
        if (!output) {
            std::cerr << "failed to open " << argv[1] << '\n';
            return 1;
        }

        output << "# name\thex\texpected diagnostic notation or ERROR\n";
        write_vectors(output);
    } catch (const std::exception &exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }

    return 0;
}
