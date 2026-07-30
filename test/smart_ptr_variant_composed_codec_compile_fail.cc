#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace composed_variant {

using namespace cbor::tags;

struct record {
    std::uint64_t value{};
};

template <typename Self> struct record_codec : cbor_codec_mixin_base<Self> {
    using cbor_codec_mixin_base<Self>::decode;

    status_code decode(record &, major_type, std::byte) { return status_code::success; }
};

} // namespace composed_variant

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::smart_ptr;

    std::vector<std::byte> bytes;
    auto                   dec = make_decoder<shared_ptr_codec, composed_variant::record_codec>(bytes);
    std::variant<std::shared_ptr<std::uint64_t>, composed_variant::record> value;
    return dec(value).has_value() ? 0 : 1;
}
