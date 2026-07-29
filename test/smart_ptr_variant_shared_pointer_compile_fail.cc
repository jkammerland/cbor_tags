#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <cstddef>
#include <memory>
#include <string>
#include <variant>
#include <vector>

int main() {
    std::vector<std::byte> bytes;
    auto                   enc = cbor::tags::make_encoder<cbor::tags::ext::smart_ptr::shared_ptr_codec>(bytes);

    std::variant<std::shared_ptr<int>, std::string> value{std::make_shared<int>(1)};
    return enc(value).has_value() ? 0 : 1;
}
