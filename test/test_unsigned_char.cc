#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_concepts_checking.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

using namespace cbor::tags;

namespace {

[[nodiscard]] std::string to_hex(const std::vector<std::byte> &data) {
    auto result = std::string{};
    char byte_hex[3]{};
    for (auto byte : data) {
        std::snprintf(byte_hex, sizeof(byte_hex), "%02x", static_cast<unsigned>(byte));
        result += byte_hex;
    }
    return result;
}

} // namespace

int main() {
    static_assert(!std::is_signed_v<char>, "this regression target must be compiled with -funsigned-char");
    static_assert(IsTextChar<char>);
    static_assert(IsTextChar<signed char>);
    static_assert(!IsTextChar<unsigned char>);
    static_assert(IsTextString<std::string>);
    static_assert(IsTextString<std::string_view>);
    static_assert(IsTextString<std::basic_string_view<char>>);
    static_assert(!IsTextString<std::vector<char>>);
    static_assert(!IsTextString<std::basic_string<unsigned char>>);
    static_assert(!IsTextString<std::basic_string_view<unsigned char>>);
    static_assert(!IsArray<std::string>);
    static_assert(IsCborMajor<std::variant<std::string, std::vector<int>>>);
    static_assert(valid_concept_mapping_v<std::variant<std::string, std::vector<int>>>);

    auto require = [](bool ok) {
        if (!ok) {
            std::abort();
        }
    };

    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);
    require(enc(std::string{"hi"}).has_value());
    require(to_hex(data) == "626869");

    auto        dec = make_decoder(data);
    std::string decoded;
    require(dec(decoded).has_value());
    require(decoded == "hi");

    auto                                        variant_data = std::vector<std::byte>{};
    auto                                        variant_enc  = make_encoder(variant_data);
    std::variant<std::string, std::vector<int>> value{std::string{"ok"}};
    require(variant_enc(value).has_value());

    auto variant_decoded = decltype(value){};
    auto variant_dec     = make_decoder(variant_data);
    require(variant_dec(variant_decoded).has_value());
    require(std::holds_alternative<std::string>(variant_decoded));
    require(std::get<std::string>(variant_decoded) == "ok");
}
