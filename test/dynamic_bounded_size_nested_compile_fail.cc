#include "cbor_tags/cbor.h"

#include <vector>

int main() {
    using inner = cbor::tags::dynamic_bounded_size<std::vector<int>>;
    cbor::tags::dynamic_bounded_size<inner> value{inner{std::vector<int>{}, 0, 4}, 0, 4};
    return value.value().value().empty() ? 0 : 1;
}
