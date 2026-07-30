#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

struct tag_28_value {
    cbor::tags::static_tag<28> cbor_tag;
    std::uint64_t              value{};
};

int main() {
    std::vector<std::byte> bytes;
    auto                   dec = cbor::tags::make_decoder<cbor::tags::ext::smart_ptr::shared_ptr_codec>(bytes);

    std::variant<std::shared_ptr<int>, tag_28_value> value;
    return dec(value).has_value() ? 0 : 1;
}
