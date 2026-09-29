#include <cbor_tags/extensions/std_expected.h>
#include <expected>
#include <optional>
#include <variant>
#include <vector>

using namespace cbor::tags;
using ext::std_expected::std_expected_codec;

struct empty {};
struct group {
    empty value;
};
struct tagged_empty {
    static constexpr std::uint64_t cbor_tag = 321;
};

template <typename Self> struct header_codec : codec_mixin_base<Self> {
    using codec_mixin_base<Self>::encode;
    using codec_mixin_base<Self>::decode;
    status_code decode(empty &, major_type major, std::byte info) {
        std::uint64_t value{};
        return static_cast<Self &>(*this).decode(value, major, info);
    }
    status_code decode(tagged_empty &, major_type major, std::byte info) {
        auto &dec    = static_cast<Self &>(*this);
        auto  status = dec.decode(static_tag<321>{}, major, info);
        if (status != status_code::success) {
            return status;
        }
        std::uint64_t value{};
        return dec.decode(value);
    }
};

template <typename Self> struct direct_codec : codec_mixin_base<Self> {
    using codec_mixin_base<Self>::encode;
    using codec_mixin_base<Self>::decode;
    status_code decode(empty &) {
        std::uint64_t value{};
        return static_cast<Self &>(*this).decode(value);
    }
};

template <typename Self> struct container_codec : codec_mixin_base<Self> {
    using codec_mixin_base<Self>::encode;
    using codec_mixin_base<Self>::decode;
    void        encode(const std::vector<empty> &value) { static_cast<Self &>(*this).encode(value.size()); }
    status_code decode(std::vector<empty> &value, major_type major, std::byte info) {
        std::size_t size{};
        auto        status = static_cast<Self &>(*this).decode(size, major, info);
        if (status == status_code::success) {
            value.resize(size);
        }
        return status;
    }
};

#if defined(CBOR_EXPECTED_HEADER_ROOT)
using payload = std::expected<empty, int>;
#elif defined(CBOR_EXPECTED_HEADER_GROUP)
using payload = std::expected<group, int>;
#elif defined(CBOR_EXPECTED_HEADER_ARRAY) || defined(CBOR_EXPECTED_DIRECT_ARRAY)
using payload = std::expected<std::vector<empty>, int>;
#elif defined(CBOR_EXPECTED_DIRECT_OPTIONAL)
using payload = std::expected<std::optional<empty>, int>;
#elif defined(CBOR_EXPECTED_TAG_VARIANT)
using payload = std::expected<std::variant<tagged_empty, int>, int>;
#elif defined(CBOR_EXPECTED_BOUNDED_CONTAINER)
using payload = std::expected<bounded_size<std::vector<empty>, 0, 2>, int>;
#elif defined(CBOR_EXPECTED_INDEFINITE_CONTAINER)
using payload = std::expected<as_indefinite<std::vector<empty>>, int>;
#endif

int main() {
    std::vector<std::byte> bytes;
#if defined(CBOR_EXPECTED_INDEFINITE_CONTAINER)
    std::vector<empty> elements;
    payload            value{std::in_place, elements};
    return make_encoder<container_codec, std_expected_codec>(bytes)(value).has_value() ? 0 : 1;
#else
    payload value{};
#if defined(CBOR_EXPECTED_BOUNDED_CONTAINER)
    return make_decoder<container_codec, std_expected_codec>(bytes)(value).has_value() ? 0 : 1;
#elif defined(CBOR_EXPECTED_DIRECT_ARRAY) || defined(CBOR_EXPECTED_DIRECT_OPTIONAL)
    return make_decoder<direct_codec, std_expected_codec>(bytes)(value).has_value() ? 0 : 1;
#else
    return make_decoder<header_codec, std_expected_codec>(bytes)(value).has_value() ? 0 : 1;
#endif
#endif
}
