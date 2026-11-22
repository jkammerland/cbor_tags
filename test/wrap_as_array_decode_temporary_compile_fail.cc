#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ct = cbor::tags;

// wrap_as_array keeps a temporary it is given. That is what an encoder wants.
// A decoder would write the value into the kept temporary and report success,
// with the variable the caller moved from left empty
int main() {
    // [7, "ab"]
    const std::vector<std::byte> input{std::byte{0x82}, std::byte{0x07}, std::byte{0x62}, std::byte{0x61}, std::byte{0x62}};

    int         id{};
    std::string name;

#if defined(CBOR_TAGS_WRAP_ARRAY_ORDINARY)
    auto decoder = ct::make_decoder(input);
    auto result  = decoder(ct::wrap_as_array{id, std::move(name)});
    return result ? 0 : 1;
#elif defined(CBOR_TAGS_WRAP_ARRAY_RESUMABLE)
    auto decoder   = ct::make_resumable_decoder(input);
    auto operation = decoder(ct::wrap_as_array{id, std::move(name)});
    return operation.done() ? 0 : 1;
#endif
}
