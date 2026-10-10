#include "../examples/cxx26_value_wrappers.h"

#include <cbor_tags/extensions/cbor_visualization.h>
#include <fmt/format.h>
#include <memory>
#include <optional>
#include <tuple>
#include <variant>
#include <vector>

using namespace cbor::tags;
using codec::std_indirect;

struct empty {};
struct group {
    int first{};
    int second{};
};

#if defined(CBOR_VALUE_DIRECT_MIXIN)
template <typename Self> struct direct_codec : codec::base<Self> {
    using codec::base<Self>::encode;
    using codec::base<Self>::decode;
    status_code decode(empty &) {
        int value{};
        return static_cast<Self &>(*this).decode(value);
    }
};
using payload = std::indirect<empty>;
#elif defined(CBOR_VALUE_EMPTY)
using payload = std::indirect<empty>;
#elif defined(CBOR_VALUE_TAG_HEADER)
using payload = std::indirect<static_tag<42>>;
#elif defined(CBOR_VALUE_UNWRAPPED_GROUP)
using payload = std::indirect<group>;
#elif defined(CBOR_VALUE_VARIANT)
using payload = std::variant<std::indirect<int>, int>;
#elif defined(CBOR_VALUE_OPTIONAL_VARIANT)
using payload = std::variant<int, std::optional<std::indirect<std::string>>>;
#elif defined(CBOR_VALUE_NESTED_VARIANT)
using payload = std::variant<int, std::variant<bool, std::indirect<std::string>>>;
#elif defined(CBOR_VALUE_DUPLICATE_TAGS)
using payload = std::variant<cbor_value_example::dog_wire, std::tuple<static_tag<60010>, std::string>>;
#elif defined(CBOR_VALUE_POLYMORPHIC_VARIANT)
using payload = std::variant<std::polymorphic<cbor_value_example::animal>, int>;
#endif

#if defined(CBOR_VALUE_UNWRAPPED_GROUP)
using payload_options = Options<default_expected>;
#else
using payload_options = default_options;
#endif

int main() {
    std::vector<std::byte> bytes{std::byte{0x01}};
    payload                value{};
#if defined(CBOR_VALUE_DIRECT_MIXIN)
    return make_decoder<direct_codec, codec::std_indirect>(bytes)(value).has_value() ? 0 : 1;
#elif defined(CBOR_VALUE_ENCODE)
    encoder<std::vector<std::byte>, payload_options, cbor_header_encoder, cbor_indefinite_encoder, cbor_optional_encoder,
            cbor_variant_encoder, codec::std_indirect, cbor_value_example::codec::animal>
        enc{bytes};
    return enc(value).has_value() ? 0 : 1;
#elif defined(CBOR_VALUE_CDDL)
    fmt::memory_buffer schema;
    cddl::schema_to<payload>(schema);
#else
    return make_decoder_with_options<payload_options, codec::std_indirect, cbor_value_example::codec::animal>(bytes)(value).has_value() ? 0
                                                                                                                                        : 1;
#endif
}
