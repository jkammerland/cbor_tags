#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::smart_ptr;

    using value_type = std::variant<std::vector<std::unique_ptr<std::uint64_t>>, std::map<std::string, std::shared_ptr<std::string>>>;

    std::vector<std::byte> bytes;
#if defined(CBOR_TAGS_ONLY_UNIQUE_CODEC)
    auto dec = make_decoder<unique_ptr_codec>(bytes);
#else
    auto dec = make_decoder<shared_ptr_codec>(bytes);
#endif
    value_type value;
    return dec(value).has_value() ? 0 : 1;
}
