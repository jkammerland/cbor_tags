#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

namespace ct = cbor::tags;

// This codec has the codec base. Its return type is deduced, so asking it
// about a type without a to_cbor() directly is an error inside its body. A
// one-shot encoder never gets that far, its own encode for a string or a
// struct fits better
template <typename Self> struct to_cbor_encoder : ct::codec::encoder_base<Self> {
    using ct::codec::encoder_base<Self>::encode;

    template <typename T> auto encode(const T &value) { return static_cast<Self &>(*this).encode(value.to_cbor()); }
};

// Has a hook of its own and no to_cbor() either
struct named {
    std::string name;

    template <typename Encoder> ct::resumable<ct::status_code> resumable_encode(Encoder &encoder) const { return encoder(name); }
};

namespace {

std::vector<std::byte> bytes_of(std::initializer_list<int> values) {
    std::vector<std::byte> bytes;
    for (const int value : values)
        bytes.push_back(static_cast<std::byte>(value));
    return bytes;
}

// A codec with the base is left alone, for a string and for a type with a hook
bool codec_with_base_is_not_asked() {
    const std::string      text{"hi"};
    const named            value{"hi"};
    std::vector<std::byte> text_bytes;
    std::vector<std::byte> value_bytes;

    auto text_encoder    = ct::make_resumable_encoder<to_cbor_encoder>(text_bytes);
    auto text_operation  = text_encoder(text);
    auto value_encoder   = ct::make_resumable_encoder<to_cbor_encoder>(value_bytes);
    auto value_operation = value_encoder(value);

    const auto expected = bytes_of({0x62, 'h', 'i'});
    const bool text_ok  = text_operation.done() && text_operation.result() && text_bytes == expected;
    const bool value_ok = value_operation.done() && value_operation.result() && value_bytes == expected;
    return text_ok && value_ok;
}

} // namespace

int main() { return codec_with_base_is_not_asked() ? 0 : 1; }
