#include <cbor_tags/cbor_decoder.h>

#include <cstddef>
#include <vector>

int main() {
    auto view = cbor::tags::find_tags<100>(std::vector<std::byte>{std::byte{0xD8}, std::byte{0x64}, std::byte{0x01}});
    auto it   = view.begin();
    return it == view.end() ? 1 : 0;
}
