#include <cbor_tags/cbor_decoder.h>

#include <cstddef>
#include <cstdio>
#include <type_traits>
#include <vector>

int main() {
    std::vector<std::byte> buffer{std::byte{0xD8}, std::byte{0x64}, std::byte{0x01}};
    auto                   view = cbor::tags::find_tags<100>(buffer);
    auto                   it   = view.begin();
    auto                   dec  = it->make_decoder();
    int                    value{};

    using decode_result = decltype(dec.decode(value));
    static_assert(std::is_same_v<decode_result, cbor::tags::status_code>);

    if (dec.decode(value) != cbor::tags::status_code::success || value != 1) {
        return 1;
    }

    std::puts("PROOF: make_decoder().decode(T&) returns raw status_code instead of expected<void,status_code>");
    return 0;
}
