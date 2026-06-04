#include <cbor_tags/cbor.h>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_reflection_config.h>
#include <cbor_tags/cwt/cwt.h>
#include <cbor_tags/extensions/rfc8746_typed_arrays.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#if !CBOR_TAGS_STL_ONLY
#include <cbor_tags/extensions/cbor_visualization.h>
#include <cbor_tags/extensions/custom_codec_1.h>
#include <fmt/format.h>
#endif

#if CBOR_TAGS_TEST_PACKAGE_HAS_CWT_OPENSSL
#include <cbor_tags/cwt/openssl_crypto.h>
#elif CBOR_TAGS_TEST_PACKAGE_HAS_CWT_WOLFSSL
#include <cbor_tags/cwt/wolfssl_crypto.h>
#endif

#if CBOR_TAGS_USE_STD_EXPECTED
#include <expected>
#else
#include <tl/expected.hpp>
#endif

enum class PackageColor : std::uint8_t { red = 1, blue = 2 };

#if CBOR_TAGS_USE_BOOST_PFR_NAMES
struct PackagePerson {
    std::uint8_t age;
    std::string  name;
};
#endif

int main() {
#if CBOR_TAGS_USE_STD_EXPECTED
    static_assert(std::is_same_v<cbor::tags::expected<void, cbor::tags::status_code>, std::expected<void, cbor::tags::status_code>>);
#else
    static_assert(std::is_same_v<cbor::tags::expected<void, cbor::tags::status_code>, tl::expected<void, cbor::tags::status_code>>);
#endif

#if CBOR_TAGS_TEST_PACKAGE_HAS_CWT_OPENSSL
    static_assert(cbor::tags::cwt::openssl_es256_backend::algorithm_id == cbor::tags::cwt::algorithm::es256);
#elif CBOR_TAGS_TEST_PACKAGE_HAS_CWT_WOLFSSL
    static_assert(cbor::tags::cwt::wolfssl_es256_backend::algorithm_id == cbor::tags::cwt::algorithm::es256);
#endif

    {
        std::vector<std::byte> bytes;
        auto                   enc = cbor::tags::make_encoder(bytes);
        if (!enc(std::uint64_t{42})) {
            return 1;
        }

        std::uint64_t decoded{};
        auto          dec = cbor::tags::make_decoder(bytes);
        if (!dec(decoded)) {
            return 2;
        }
        if (decoded != 42U) {
            return 3;
        }
    }

    {
        namespace cwt = cbor::tags::cwt;

        auto claims_bytes = cwt::encode_to_bytes(cwt::claims_set{.issuer = "issuer", .subject = "subject"});
        if (!claims_bytes) {
            return 11;
        }

        cwt::claims_set decoded;
        auto            dec = cbor::tags::make_decoder(*claims_bytes);
        if (!dec(decoded)) {
            return 12;
        }
        if (!decoded.issuer || *decoded.issuer != "issuer" || !decoded.subject || *decoded.subject != "subject") {
            return 13;
        }
    }

    {
        namespace rfc8746 = cbor::tags::ext::rfc8746;

        std::vector<std::byte>    bytes;
        std::vector<std::int32_t> values{1, -2, 3};
        auto                      enc = cbor::tags::make_encoder<rfc8746::typed_array_codec>(bytes);
        if (!enc(rfc8746::as_typed_array(values))) {
            return 4;
        }

        rfc8746::typed_array<std::int32_t> decoded;
        auto                               dec = cbor::tags::make_decoder<rfc8746::typed_array_codec>(bytes);
        if (!dec(decoded) || decoded.values() != values) {
            return 5;
        }

#if !CBOR_TAGS_STL_ONLY
        fmt::memory_buffer schema;
        cbor::tags::cddl_schema_to<rfc8746::typed_array<std::int32_t>>(schema, {.row_options = {.format_by_rows = false}});
        if (fmt::to_string(schema) != "root = #6.78(bstr)") {
            return 6;
        }
#endif
    }

#if !CBOR_TAGS_STL_ONLY
    {
        namespace compact = cbor::tags::ext::custom_codec_1;

        std::vector<std::byte>    compact_bytes;
        std::vector<std::int16_t> values{1, -2, 3};
        auto                      enc = cbor::tags::make_encoder<compact::custom_codec_1>(compact_bytes);
        if (!enc(compact::as_custom_codec_1(cbor::tags::static_tag<17>{}, values))) {
            return 7;
        }

        std::vector<std::int16_t> decoded;
        auto                      dec = cbor::tags::make_decoder<compact::custom_codec_1>(compact_bytes);
        if (!dec(compact::as_custom_codec_1(cbor::tags::static_tag<17>{}, decoded)) || decoded != values) {
            return 8;
        }
    }
#endif

#if CBOR_TAGS_USE_MAGIC_ENUM_NAMES
    {
        fmt::memory_buffer schema;
        cbor::tags::cddl_schema_to<PackageColor>(
            schema, {.row_options = {.format_by_rows = false}, .enum_mode = cbor::tags::CDDLEnumMode::named_values});
        const auto text = fmt::to_string(schema);
        if (text.find("red") == std::string::npos || text.find("blue") == std::string::npos) {
            return 9;
        }
    }
#endif

#if CBOR_TAGS_USE_BOOST_PFR_NAMES
    {
        static_assert(CBOR_TAGS_HAS_BOOST_PFR_NAMES == 1);

        fmt::memory_buffer schema;
        cbor::tags::cddl_schema_to<cbor::tags::as_named_map<PackagePerson>>(
            schema, {.row_options = {.format_by_rows = false}, .root_name = "person"});
        const auto text = fmt::to_string(schema);
        if (text.find("age: uint") == std::string::npos || text.find("name: tstr") == std::string::npos) {
            return 10;
        }
    }
#endif

    return 0;
}
