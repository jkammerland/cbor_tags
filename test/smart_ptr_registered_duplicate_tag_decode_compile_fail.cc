#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <type_traits>
#include <vector>

namespace duplicate_decode_tag {

using namespace cbor::tags;
using namespace cbor::tags::ext::smart_ptr;

struct base {
    virtual ~base() = default;
};

struct first final : base {
    std::uint64_t value{};

    template <typename Decoder> auto decode(Decoder &dec) { return dec(value); }
};

struct second final : base {
    std::uint64_t value{};

    template <typename Decoder> auto decode(Decoder &dec) { return dec(value); }
};

constexpr auto cbor_tag(const first &) { return static_tag<100>{}; }
constexpr auto cbor_tag(const second &) { return static_tag<100>{}; }

constexpr auto cbor_smart_pointer_pointee_types(pointee_types_for<std::shared_ptr<base>>) {
    return std::type_identity<std::tuple<first, second>>{};
}

} // namespace duplicate_decode_tag

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::smart_ptr;

    std::vector<std::byte>                      bytes;
    auto                                        dec = make_decoder<shared_ptr_codec>(bytes);
    std::shared_ptr<duplicate_decode_tag::base> value;
    return dec(value).has_value() ? 0 : 1;
}
