#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

struct custom_record {
    std::uint64_t first{};
    std::uint64_t second{};

    template <typename Encoder> constexpr auto encode(Encoder &enc) const { return enc(first, second); }
};

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::smart_ptr;

    std::vector<std::byte> bytes;
    auto                   enc = make_encoder<shared_ptr_codec>(bytes);
    return enc(std::make_shared<custom_record>()).has_value() ? 0 : 1;
}
