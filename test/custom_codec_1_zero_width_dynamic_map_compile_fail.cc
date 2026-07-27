#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/custom_codec_1.h"

#include <compare>
#include <cstddef>
#include <map>
#include <vector>

struct zero_width_key {
    constexpr auto operator<=>(const zero_width_key &) const = default;
};

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::custom_codec_1;

    std::vector<std::byte>                   output;
    std::map<zero_width_key, std::nullptr_t> values;
    auto                                     enc = make_encoder<custom_codec_1>(output);
    return enc(as_custom_codec_1(static_tag<1>{}, values)).has_value() ? 0 : 1;
}
