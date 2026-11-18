#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/codec.h"
#include "cbor_tags/codec/unique_ptr.h"

#include <cstddef>
#include <memory>
#include <string>
#include <variant>
#include <vector>

struct position {
    int x{};
    int y{};
};

// y comes first on the wire. Only decode(position &), the form for a decoder
// that stands on the first byte, and the decoder steps back to call it
template <typename Self> struct y_first : cbor::tags::codec::decoder_base<Self> {
    using cbor::tags::codec::decoder_base<Self>::decode;

    cbor::tags::status_code decode(position &value) {
        return static_cast<Self &>(*this).decode(cbor::tags::wrap_as_array{value.y, value.x});
    }
};

// A variant with a pointer in it picks the alternative by what the types look
// like on the wire, and what a codec reads is the codec's business
int main() {
    using choice = std::variant<std::unique_ptr<position>, std::string>;

    const std::vector<std::byte> bytes{std::byte{0x82}, std::byte{0x07}, std::byte{0x03}};
    choice                       value;

    auto       decoder = cbor::tags::make_decoder<cbor::tags::codec::unique_ptr, y_first>(bytes);
    const auto result  = decoder(value);

    return result.has_value() ? 0 : 1;
}
