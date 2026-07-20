#include <array>
#include <cbor_tags/extensions/cbor_visualization.h>
#include <cstddef>
#include <span>
#include <string>

using namespace cbor::tags;

#if defined(CBOR_TAGS_BOUNDED_FIXED_BYTE_ARRAY)
using invalid_bounded_string = bounded_size<std::array<std::byte, 2>, 3, 4>;
#elif defined(CBOR_TAGS_BOUNDED_FIXED_TEXT_SPAN)
using invalid_bounded_string = bounded_size<std::span<const char, 2>, 3, 4>;
#else
#error "A fixed string test shape must be selected"
#endif

int main() {
    std::string schema;
    cddl_schema_to<invalid_bounded_string>(schema);
    return 0;
}
