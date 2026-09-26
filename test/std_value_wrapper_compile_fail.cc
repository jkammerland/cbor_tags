#include "../examples/cxx26_value_wrappers.h"

#include <cbor_tags/extensions/cbor_visualization.h>
#include <memory>
#include <tuple>
#include <variant>
#include <vector>

using namespace cbor::tags;
using cbor_value_example::animal_codec;
using ext::std_indirect::std_indirect_codec;

struct empty {};
struct group {
    int first{};
    int second{};
};

#if defined(CBOR_VALUE_EMPTY)
using payload = std::indirect<empty>;
#elif defined(CBOR_VALUE_TAG_HEADER)
using payload = std::indirect<static_tag<42>>;
#elif defined(CBOR_VALUE_UNWRAPPED_GROUP)
using payload = std::indirect<group>;
#elif defined(CBOR_VALUE_VARIANT)
using payload = std::variant<std::indirect<int>, int>;
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
#if defined(CBOR_VALUE_ENCODE)
    encoder<std::vector<std::byte>, payload_options, cbor_header_encoder, cbor_indefinite_encoder, cbor_optional_encoder,
            cbor_variant_encoder, std_indirect_codec, animal_codec>
        enc{bytes};
    return enc(value).has_value() ? 0 : 1;
#elif defined(CBOR_VALUE_CDDL)
    fmt::memory_buffer schema;
    cddl_schema_to<payload>(schema);
#else
    return make_decoder_with_options<payload_options, std_indirect_codec, animal_codec>(bytes)(value).has_value() ? 0 : 1;
#endif
}
