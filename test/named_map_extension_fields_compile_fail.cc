#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/cbor_visualization.h"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace {

using StringExtension = cbor::tags::as_named_extension<std::map<std::string, std::string>>;

struct RootRootExtensions {
    int             id;
    StringExtension first;
    StringExtension second;
};

struct GroupWithExtension {
    StringExtension extra;
};

struct RootGroupExtension {
    int                                            id;
    cbor::tags::as_named_group<GroupWithExtension> group;
    StringExtension                                extra;
};

struct FirstGroupWithExtension {
    StringExtension first;
};

struct SecondGroupWithExtension {
    StringExtension second;
};

struct RootTwoGroupExtensions {
    int                                                  id;
    cbor::tags::as_named_group<FirstGroupWithExtension>  first;
    cbor::tags::as_named_group<SecondGroupWithExtension> second;
};

#if defined(CBOR_TAGS_EXTENSION_FIELDS_ROOT_ROOT)
using TestRoot = RootRootExtensions;
#elif defined(CBOR_TAGS_EXTENSION_FIELDS_ROOT_GROUP)
using TestRoot = RootGroupExtension;
#elif defined(CBOR_TAGS_EXTENSION_FIELDS_GROUP_GROUP)
using TestRoot = RootTwoGroupExtensions;
#else
#error "expected an extension-field compile-fail shape"
#endif

} // namespace

int main() {
#if defined(CBOR_TAGS_EXTENSION_FIELDS_ENCODE)
    TestRoot               value{};
    std::vector<std::byte> output;
    auto                   enc = cbor::tags::make_encoder(output);
    (void)enc(cbor::tags::as_named_map{value});
#elif defined(CBOR_TAGS_EXTENSION_FIELDS_DECODE)
    std::vector<std::byte> input{std::byte{0xA0}};
    TestRoot               value{};
    auto                   dec = cbor::tags::make_decoder(input);
    (void)dec(cbor::tags::as_named_map{value});
#elif defined(CBOR_TAGS_EXTENSION_FIELDS_CDDL)
    fmt::memory_buffer output;
    cbor::tags::cddl_schema_to<cbor::tags::as_named_map<TestRoot>>(output, {.root_name = "bad"});
#else
#error "expected an extension-field compile-fail mode"
#endif
}
