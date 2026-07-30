#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <type_traits>
#include <vector>

namespace value_tag {

using namespace cbor::tags;
using namespace cbor::tags::ext::smart_ptr;

struct base {
    virtual ~base() = default;
};

struct child final : base {
    std::uint64_t tag{};
    std::uint64_t value{};

    template <typename Encoder> auto encode(Encoder &enc) const { return enc(value); }
};

constexpr std::uint64_t cbor_tag(const child &value) { return value.tag; }

constexpr auto cbor_smart_pointer_pointee_types(pointee_types_for<std::shared_ptr<base>>) {
    return std::type_identity<std::tuple<child>>{};
}

} // namespace value_tag

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::smart_ptr;

    std::vector<std::byte>           bytes;
    auto                             enc   = make_encoder<shared_ptr_codec>(bytes);
    std::shared_ptr<value_tag::base> value = std::make_shared<value_tag::child>();
    return enc(value).has_value() ? 0 : 1;
}
