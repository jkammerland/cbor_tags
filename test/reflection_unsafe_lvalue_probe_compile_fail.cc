#include <cbor_tags/cbor_reflection.h>
#include <memory>
#include <vector>

struct mixed_reference_move_only_container {
    int                              &reference;
    std::vector<std::unique_ptr<int>> children;
};

int main() {
    int                                 reference{};
    mixed_reference_move_only_container value{reference, {}};
    (void)cbor::tags::to_tuple(value);
}
