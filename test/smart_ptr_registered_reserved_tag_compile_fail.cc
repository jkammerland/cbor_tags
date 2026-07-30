#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <type_traits>
#include <vector>

namespace reserved_tag {

using namespace cbor::tags;
using namespace cbor::tags::ext::smart_ptr;

struct base {
    virtual ~base() = default;
};

struct child final : base {
    std::uint64_t value{};

    template <typename Encoder> auto encode(Encoder &enc) const { return enc(value); }
};

constexpr auto cbor_tag(const child &) { return static_tag<28>{}; }

constexpr auto cbor_smart_pointer_pointee_types(pointee_types_for<std::shared_ptr<base>>) {
    return std::type_identity<std::tuple<child>>{};
}

} // namespace reserved_tag

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::smart_ptr;

    std::vector<std::byte>              bytes;
    auto                                enc   = make_encoder<shared_ptr_codec>(bytes);
    std::shared_ptr<reserved_tag::base> value = std::make_shared<reserved_tag::child>();
    return enc(value).has_value() ? 0 : 1;
}
