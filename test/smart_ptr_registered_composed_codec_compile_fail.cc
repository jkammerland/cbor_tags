#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <type_traits>
#include <vector>

namespace registered_composed {

using namespace cbor::tags;
using namespace cbor::tags::ext::smart_ptr;

struct base {
    virtual ~base() = default;
};

struct child final : base {
    std::uint64_t value{};
};

constexpr auto cbor_tag(const child &) { return static_tag<100>{}; }

constexpr auto cbor_smart_pointer_pointee_types(pointee_types_for<std::shared_ptr<base>>) {
    return std::type_identity<std::tuple<child>>{};
}

template <typename Self> struct child_codec : cbor_codec_mixin_base<Self> {
    using cbor_codec_mixin_base<Self>::decode;
    using cbor_codec_mixin_base<Self>::encode;

    status_code decode(child &, major_type, std::byte) { return status_code::success; }
    void        encode(const child &) {}
};

} // namespace registered_composed

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::smart_ptr;

    std::vector<std::byte> bytes;
#if defined(CBOR_TAGS_REGISTERED_COMPOSED_ENCODE)
    auto                                       enc   = make_encoder<shared_ptr_codec, registered_composed::child_codec>(bytes);
    std::shared_ptr<registered_composed::base> value = std::make_shared<registered_composed::child>();
    return enc(value).has_value() ? 0 : 1;
#else
    auto                                       dec = make_decoder<shared_ptr_codec, registered_composed::child_codec>(bytes);
    std::shared_ptr<registered_composed::base> value;
    return dec(value).has_value() ? 0 : 1;
#endif
}
