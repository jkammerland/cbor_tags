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
template <typename Self> struct unrelated_codec : codec_mixin_base<Self> {
    using codec_mixin_base<Self>::encode;
    using codec_mixin_base<Self>::decode;
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

#if defined(CBOR_TAGS_EXPECTED_CONVERTING_MIXIN)
struct empty;
struct converted {
    converted() = default;
    converted(const empty &) {}
};
struct empty {
    operator converted &() {
        static converted value;
        return value;
    }
};
template <typename Self> struct payload_codec : codec_mixin_base<Self> {
    using codec_mixin_base<Self>::encode;
    using codec_mixin_base<Self>::decode;
    void        encode(const converted &) { static_cast<Self &>(*this).encode(nullptr); }
    status_code decode(converted &) {
        std::nullptr_t value{};
        return static_cast<Self &>(*this).decode(value);
    }
    status_code decode(converted &, major_type major, std::byte info) {
        std::nullptr_t value{};
        return static_cast<Self &>(*this).decode(value, major, info);
    }
};
#else
struct empty {};
#if defined(CBOR_TAGS_EXPECTED_GENERIC_MIXIN) || defined(CBOR_TAGS_EXPECTED_GENERIC_ARRAY_MIXIN)
template <typename Self> struct payload_codec : codec_mixin_base<Self> {
    using codec_mixin_base<Self>::encode;
    using codec_mixin_base<Self>::decode;
    template <typename T> void        encode(const T &) { static_cast<Self &>(*this).encode(nullptr); }
    template <typename T> status_code decode(T &) {
        std::nullptr_t value{};
        return static_cast<Self &>(*this).decode(value);
    }
#if defined(CBOR_TAGS_EXPECTED_GENERIC_ARRAY_MIXIN)
    template <IsRangeOfCborValues T>
#else
    template <typename T>
#endif
    status_code decode(T &, major_type major, std::byte info) {
        std::nullptr_t value{};
        return static_cast<Self &>(*this).decode(value, major, info);
    }
};
#elif defined(CBOR_TAGS_EXPECTED_MUTABLE_MIXIN)
template <typename Self> struct payload_codec : codec_mixin_base<Self> {
    using codec_mixin_base<Self>::encode;
    using codec_mixin_base<Self>::decode;
    void encode(empty &) { static_cast<Self &>(*this).encode(nullptr); }
};
#elif defined(CBOR_TAGS_EXPECTED_CONST_MIXIN)
template <typename Self> struct payload_codec : codec_mixin_base<Self> {
    using codec_mixin_base<Self>::encode;
    using codec_mixin_base<Self>::decode;
    status_code decode(const empty &) {
        std::nullptr_t value{};
        return static_cast<Self &>(*this).decode(value);
    }
    status_code decode(const empty &, major_type major, std::byte info) {
        std::nullptr_t value{};
        return static_cast<Self &>(*this).decode(value, major, info);
    }
};
#else
template <typename Self> using payload_codec = unrelated_codec<Self>;
#endif
#endif
struct group {
    int first{};
    int second{};
};

#if defined(CBOR_TAGS_EXPECTED_GENERIC_ARRAY_MIXIN)
using payload = std::expected<std::vector<empty>, int>;
#elif defined(CBOR_TAGS_EXPECTED_CONVERTING_MIXIN) || defined(CBOR_TAGS_EXPECTED_GENERIC_MIXIN) ||                                         \
    defined(CBOR_TAGS_EXPECTED_MUTABLE_MIXIN) || defined(CBOR_TAGS_EXPECTED_CONST_MIXIN)
using payload = std::expected<empty, empty>;
#elif defined(CBOR_TAGS_EXPECTED_EMPTY_VALUE)
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
            cbor_variant_encoder, payload_codec, std_expected_codec>
        enc{bytes};
    return enc(value).has_value() ? 0 : 1;
#else
    return make_decoder_with_options<payload_options, payload_codec, std_expected_codec>(bytes)(value).has_value() ? 0 : 1;
#endif
}
