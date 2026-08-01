#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_extensions.h"
#include "cbor_tags/detail/cbor_extension_decode.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

struct record {
    static constexpr std::uint64_t cbor_tag = 42U;
    std::uint64_t                  value{};
};

template <typename Self> struct record_codec : cbor::tags::cbor_codec_mixin_base<Self> {
    using cbor::tags::cbor_codec_mixin_base<Self>::decode;
    using cbor::tags::cbor_codec_mixin_base<Self>::encode;

    void encode(const record &value) {
        auto &enc = static_cast<Self &>(*this);
        enc.encode(cbor::tags::static_tag<100>{});
        enc.encode(value.value);
    }

    [[nodiscard]] cbor::tags::status_code decode(record &value, cbor::tags::major_type major, std::byte additional_info) {
        if (major != cbor::tags::major_type::Tag) {
            return cbor::tags::status_code::no_match_for_tag_on_buffer;
        }
        auto         &dec = static_cast<Self &>(*this);
        std::uint64_t tag{};
        const auto    status = cbor::tags::detail::decode_unsigned_argument(dec, additional_info, tag);
        if (status != cbor::tags::status_code::success || tag != 100U) {
            return status == cbor::tags::status_code::success ? cbor::tags::status_code::no_match_for_tag : status;
        }
        return dec.decode(value.value);
    }
};

int main() {
    using choice = std::variant<std::unique_ptr<std::string>, record>;
    const std::vector<std::byte> bytes{std::byte{0xD8}, std::byte{0x64}, std::byte{0x01}};
    auto                         dec = cbor::tags::make_decoder<cbor::tags::ext::smart_ptr::unique_ptr_codec, record_codec>(bytes);
    choice                       value;
    return dec(value).has_value() ? 0 : 1;
}
