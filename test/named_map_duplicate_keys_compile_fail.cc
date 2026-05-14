#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#if defined(CBOR_TAGS_DUPLICATE_KEYS_CDDL)
#include "cbor_tags/extensions/cbor_visualization.h"

#include <fmt/format.h>
#endif

#include <cstddef>
#include <vector>

using namespace cbor::tags;

struct DuplicateGroup {
    int value;
};

struct DuplicateRoot {
    as_named_group<DuplicateGroup> group;
    int                            value;
};

int main() {
#if defined(CBOR_TAGS_DUPLICATE_KEYS_ENCODE)
    DuplicateRoot          input{.group = as_named_group<DuplicateGroup>{DuplicateGroup{.value = 7}}, .value = 9};
    std::vector<std::byte> buffer;
    auto                   enc    = make_encoder(buffer);
    auto                   result = enc(as_named_map{input});
    (void)result;
#elif defined(CBOR_TAGS_DUPLICATE_KEYS_DECODE)
    std::vector<std::byte> input{std::byte{0xA1}, std::byte{0x65}, std::byte{'v'}, std::byte{'a'},
                                 std::byte{'l'},  std::byte{'u'},  std::byte{'e'}, std::byte{0x07}};
    DuplicateRoot          decoded{};
    auto                   dec    = make_decoder(input);
    auto                   result = dec(as_named_map{decoded});
    (void)result;
#elif defined(CBOR_TAGS_DUPLICATE_KEYS_CDDL)
    fmt::memory_buffer buffer;
    cddl_schema_to<as_named_map<DuplicateRoot>>(buffer, {.row_options = {.format_by_rows = false}, .root_name = "DuplicateRoot"});
#else
#error "expected a duplicate-key compile-fail mode"
#endif
    return 0;
}
