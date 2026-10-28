#include <array>
#include <cassert>
#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <cstdint>
#include <list>
#include <span>
#include <string>
#include <vector>

int main() {
    namespace ct = cbor::tags;
    struct message {
        std::string name;
        std::vector<std::uint32_t> values;
    } value;

    std::list<std::span<const std::byte>> chunks;
    auto source = ct::make_input_source(chunks);
    auto operation = ct::make_resumable_decoder(source)(value);

    // ["abc", [1, 24]], split inside both text and the final integer.
    const std::array first{std::byte{0x82}, std::byte{0x63}, std::byte{0x61}};
    const std::array second{std::byte{0x62}, std::byte{0x63}, std::byte{0x82}, std::byte{0x01},
        std::byte{0x18}};
    const std::array third{std::byte{0x18}};
    const auto first_state = operation(first);
    const auto second_state = operation(second);
    assert(first_state == ct::resume_state::need_input);
    assert(second_state == ct::resume_state::need_input);
    (void)first_state;
    (void)second_state;
    operation(third, ct::end_of_input);

    const auto result = operation.result();
    return result && *result == 9 && value.name == "abc" &&
                   value.values == std::vector<std::uint32_t>{1, 24}
               ? 0
               : 1;
}
