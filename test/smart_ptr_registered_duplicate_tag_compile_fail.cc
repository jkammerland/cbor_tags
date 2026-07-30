#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <type_traits>
#include <vector>

namespace registered_duplicate_tag {

using namespace cbor::tags;
using namespace cbor::tags::ext::smart_ptr;

struct base {
    virtual ~base() = default;
};

struct first final : base {
    std::uint64_t value{};

    template <typename Encoder> auto encode(Encoder &enc) const { return enc(value); }
};

struct second final : base {
    std::uint64_t value{};

    template <typename Encoder> auto encode(Encoder &enc) const { return enc(value); }
};

constexpr auto cbor_tag(const first &) { return static_tag<100>{}; }
constexpr auto cbor_tag(const second &) { return static_tag<100>{}; }

constexpr auto cbor_smart_pointer_pointee_types(pointee_types_for<std::shared_ptr<base>>) {
    return std::type_identity<std::tuple<first, second>>{};
}

} // namespace registered_duplicate_tag

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::smart_ptr;
    using namespace registered_duplicate_tag;

    std::vector<std::byte> bytes;
    auto                   enc   = make_encoder<shared_ptr_codec>(bytes);
    std::shared_ptr<base>  value = std::make_shared<first>();
    return enc(value).has_value() ? 0 : 1;
}
