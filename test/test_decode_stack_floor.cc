#include "cbor_tags/cbor_decoder.h"

#include <cstddef>
#include <cstdio>
#include <vector>

using namespace cbor::tags;

namespace {

constexpr std::size_t stack_regression_floor_depth = 1024;

std::size_t nodes_entered{};
std::size_t nodes_completed{};

struct recursive_array {
    std::vector<recursive_array> children;

    template <typename Decoder> expected<void, status_code> decode(Decoder &decoder) {
        ++nodes_entered;
        auto result = decoder(children);
        if (result) {
            ++nodes_completed;
        }
        return result;
    }
};

} // namespace

int main() {
    std::vector<std::byte> input(stack_regression_floor_depth, std::byte{0x81});
    input.push_back(std::byte{0x80});

    {
        recursive_array value;
        auto            decoder = make_decoder(input);
        auto            result  = decoder(value);
        if (!result) {
            std::fprintf(stderr, "decode failed with status %u\n", static_cast<unsigned>(result.error()));
            return 1;
        }

        constexpr auto expected_nodes = stack_regression_floor_depth + 1U;
        if (nodes_entered != expected_nodes || nodes_completed != expected_nodes) {
            std::fprintf(stderr, "decoded %zu/%zu nodes; expected %zu\n", nodes_entered, nodes_completed, expected_nodes);
            return 2;
        }
    }

    return 0;
}
