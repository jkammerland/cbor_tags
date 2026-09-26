#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/std_expected.h>
#include <cstddef>
#include <expected>
#include <optional>
#include <vector>

using namespace cbor::tags;
using namespace cbor::tags::ext::std_expected;

struct unrelated {};
template <typename Self> struct unrelated_codec : cbor_codec_mixin_base<Self> {
    using cbor_codec_mixin_base<Self>::encode;
    using cbor_codec_mixin_base<Self>::decode;
    void        encode(const unrelated &) { static_cast<Self &>(*this).encode(nullptr); }
    status_code decode(unrelated &) {
        std::nullptr_t payload{};
        return static_cast<Self &>(*this).decode(payload);
    }
    status_code decode(unrelated &, major_type major, std::byte info) {
        std::nullptr_t payload{};
        return static_cast<Self &>(*this).decode(payload, major, info);
    }
};

struct empty {};
struct group {
    int first{};
    int second{};
};

#if defined(CBOR_TAGS_EXPECTED_EMPTY_VALUE)
using payload = std::expected<empty, int>;
#elif defined(CBOR_TAGS_EXPECTED_EMPTY_ERROR)
using payload = std::expected<int, empty>;
#elif defined(CBOR_TAGS_EXPECTED_OPTIONAL_EMPTY)
using payload = std::expected<std::optional<empty>, int>;
#elif defined(CBOR_TAGS_EXPECTED_CONTAINER_EMPTY)
using payload = std::expected<std::vector<empty>, int>;
#elif defined(CBOR_TAGS_EXPECTED_BOUNDED_EMPTY)
using payload = std::expected<bounded_size<std::vector<empty>, 0, 2>, int>;
#elif defined(CBOR_TAGS_EXPECTED_UNWRAPPED_GROUP)
using payload = std::expected<group, int>;
#elif defined(CBOR_TAGS_EXPECTED_TAG_HEADER)
using payload = std::expected<static_tag<42>, int>;
#endif

#if defined(CBOR_TAGS_EXPECTED_UNWRAPPED_GROUP)
using payload_options = Options<default_expected>;
#else
using payload_options = default_options;
#endif

int main() {
    std::vector<std::byte> bytes;
    payload                value{};
#if defined(CBOR_TAGS_EXPECTED_ENCODE)
    encoder<std::vector<std::byte>, payload_options, cbor_header_encoder, cbor_indefinite_encoder, cbor_optional_encoder,
            cbor_variant_encoder, unrelated_codec, std_expected_codec>
        enc{bytes};
    return enc(value).has_value() ? 0 : 1;
#else
    return make_decoder_with_options<payload_options, unrelated_codec, std_expected_codec>(bytes)(value).has_value() ? 0 : 1;
#endif
}
