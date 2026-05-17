#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_tags_config.h"
#include "cbor_tags/extensions/cbor_visualization.h"
#include "cbor_tags/extensions/rfc8746_typed_arrays.h"
#include "cbor_tags/float16_ieee754.h"

#include <array>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <fmt/format.h>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <test_util.h>
#include <tuple>
#include <variant>
#include <vector>

using namespace cbor::tags;

namespace cbor_tags_test_cddl {
template <typename T> std::string cddl_schema_inline() {
    fmt::memory_buffer buffer;
    cddl_schema_to<T>(buffer, {.row_options = {.format_by_rows = false}});
    return fmt::to_string(buffer);
}

template <typename T> std::string cddl_schema_with_options(CDDLOptions options) {
    fmt::memory_buffer buffer;
    cddl_schema_to<T>(buffer, options);
    return fmt::to_string(buffer);
}

template <typename T> void check_cddl_typed_array_tag(std::uint64_t tag) {
    CHECK_EQ(cddl_schema_inline<T>(), fmt::format("root = #6.{}(bstr)", tag));
}

std::size_t count_occurrences(std::string_view haystack, std::string_view needle) {
    std::size_t count = 0;
    std::size_t pos   = 0;
    while ((pos = haystack.find(needle, pos)) != std::string_view::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

std::string repeated_vector_cddl(std::string_view leaf, std::size_t depth) {
    std::string result{leaf};
    for (std::size_t i = 0; i < depth; ++i) {
        result = "[* " + result + "]";
    }
    return result;
}

struct CDDLPlainTwo {
    int         id;
    std::string name;
};

struct CDDLTaggedOne {
    static_tag<42> cbor_tag;
    std::string    value;
};

struct CDDLInlineTaggedOne {
    static constexpr std::uint64_t cbor_tag = 43;
    std::string                    value;
};

struct CDDLDynamicTaggedOne {
    dynamic_tag<std::uint64_t> cbor_tag;
    int                        value;
};

struct CDDLContainers {
    std::vector<int>                values;
    std::map<int, std::string>      names;
    std::array<int, 2>              pair;
    std::optional<CDDLPlainTwo>     maybe_plain;
    std::variant<int, CDDLPlainTwo> either_plain;
};

struct CDDLTypedArrays {
    cbor::tags::ext::rfc8746::typed_array<std::int32_t>                     samples;
    cbor::tags::ext::rfc8746::typed_array_be<double>                        measurements;
    cbor::tags::ext::rfc8746::homogeneous_array<std::vector<std::uint64_t>> homogeneous;
    cbor::tags::ext::rfc8746::multi_dimensional_array<std::vector<std::uint64_t>, cbor::tags::ext::rfc8746::typed_array<std::uint16_t>>
        matrix;
};

struct CDDLLooksLikeTypedArray {
    using value_type                              = int;
    static constexpr auto          byte_order     = cbor::tags::ext::rfc8746::typed_array_byte_order::little;
    static constexpr std::uint64_t cbor_array_tag = 999;
    int                            value;
};

struct CDDLLooksLikeTypedArrayRoot {
    CDDLLooksLikeTypedArray fake;
    int                     other;
};

struct CDDLNullablePointers {
    std::unique_ptr<std::uint64_t>             count;
    std::shared_ptr<std::string>               name;
    std::vector<std::shared_ptr<CDDLPlainTwo>> history;
};

using CDDLVariantWithSmartPointer       = std::variant<std::shared_ptr<int>, std::string>;
using CDDLVariantWithNestedSmartPointer = std::variant<std::vector<std::shared_ptr<int>>, std::string>;

template <typename T, std::size_t Depth> struct CDDLDeepVector {
    using type = std::vector<typename CDDLDeepVector<T, Depth - 1U>::type>;
};

template <typename T> struct CDDLDeepVector<T, 0U> {
    using type = T;
};

using CDDLVariantWithDeepSmartPointer = std::variant<typename CDDLDeepVector<std::shared_ptr<int>, 10U>::type, std::string>;
using CDDLVariantWithDeepValue        = std::variant<typename CDDLDeepVector<int, 10U>::type, std::string>;

struct CDDLTaggedNullablePointerAlternative {
    static_tag<7>        cbor_tag;
    std::shared_ptr<int> value;
};

using CDDLVariantWithTaggedSmartPointer = std::variant<CDDLTaggedNullablePointerAlternative, std::string>;

struct CDDLNullablePointerRecursiveNode {
    std::uint64_t                                     id;
    std::shared_ptr<CDDLNullablePointerRecursiveNode> next;
};

struct CDDLRecursiveNode {
    std::vector<CDDLRecursiveNode> children;
};

using CDDLVariantWithRecursiveSmartPointer = std::variant<CDDLNullablePointerRecursiveNode, std::string>;
using CDDLVariantWithRecursiveValue        = std::variant<CDDLRecursiveNode, std::string>;

struct CDDLCustomDeleter {
    void operator()(int *) const noexcept {}
};

static_assert(detail::IsNullablePointer<std::unique_ptr<int>>);
static_assert(detail::IsNullablePointer<std::unique_ptr<int, CDDLCustomDeleter>>);
static_assert(detail::IsNullablePointer<std::shared_ptr<int>>);
static_assert(detail::is_supported_nullable_pointer_v<std::unique_ptr<int>>);
static_assert(detail::is_supported_nullable_pointer_v<std::shared_ptr<int>>);
static_assert(!detail::is_supported_nullable_pointer_v<std::unique_ptr<int, CDDLCustomDeleter>>);
static_assert(!detail::is_supported_nullable_pointer_v<std::shared_ptr<void>>);
static_assert(!detail::is_supported_nullable_pointer_v<std::shared_ptr<const int>>);
static_assert(!detail::is_supported_nullable_pointer_v<std::shared_ptr<int[]>>);
static_assert(detail::cddl_contains_nullable_pointer<CDDLVariantWithSmartPointer>());
static_assert(detail::cddl_contains_nullable_pointer<CDDLVariantWithNestedSmartPointer>());
static_assert(detail::cddl_contains_nullable_pointer<CDDLVariantWithDeepSmartPointer>());
static_assert(detail::cddl_contains_nullable_pointer<CDDLVariantWithTaggedSmartPointer>());
static_assert(detail::cddl_contains_nullable_pointer<CDDLVariantWithRecursiveSmartPointer>());
static_assert(!detail::cddl_contains_nullable_pointer<CDDLVariantWithDeepValue>());
static_assert(!detail::cddl_contains_nullable_pointer<CDDLVariantWithRecursiveValue>());
static_assert(!detail::cddl_contains_nullable_pointer<std::variant<int, std::string>>());

enum class CDDLUnsignedEnum : std::uint8_t {};
enum class CDDLSignedEnum : std::int8_t {};
enum class CDDLTrafficLight : std::uint8_t { red = 1, yellow = 2, green = 4 };
enum class CDDLSignedChoice : std::int8_t { negative = -2, zero = 0, positive = 3 };
enum class CDDLUnorderedChoice : std::int8_t { high = 5, low = 1, negative = -1 };
enum class CDDLWideMagicEnum : std::uint16_t { low = 1, high = 1000 };

#if CBOR_TAGS_HAS_MAGIC_ENUM_NAMES
} // namespace cbor_tags_test_cddl

template <> struct magic_enum::customize::enum_range<cbor_tags_test_cddl::CDDLWideMagicEnum> {
    static constexpr int min = 0;
    static constexpr int max = 1000;
};

namespace cbor_tags_test_cddl {
#endif

struct CDDLEnums {
    CDDLUnsignedEnum unsigned_enum;
    CDDLSignedEnum   signed_enum;
};

struct CDDLEnumNames {
    CDDLTrafficLight                        light;
    std::optional<CDDLSignedChoice>         maybe_choice;
    std::variant<CDDLTrafficLight, int>     either_light_or_number;
    std::map<CDDLTrafficLight, std::string> labels;
    CDDLUnsignedEnum                        empty_enum;
};

struct CDDLEnumNestedLeaf {
    CDDLTrafficLight light;
};

struct CDDLEnumNestedMiddle {
    CDDLEnumNestedLeaf              leaf;
    std::optional<CDDLSignedChoice> maybe_choice;
};

struct CDDLEnumNestedRoot {
    CDDLEnumNestedMiddle          middle;
    std::vector<CDDLTrafficLight> history;
};

struct CDDLNestedLeaf {
    int         id;
    std::string name;
};

struct CDDLNestedTagged {
    static_tag<9>  cbor_tag;
    CDDLNestedLeaf leaf;
};

struct CDDLNestedChoices {
    std::vector<std::optional<CDDLNestedLeaf>>                                values;
    std::map<std::variant<int, std::string>, std::optional<CDDLNestedTagged>> lookup;
};

namespace cddl_left {
struct Thing {
    int value;
};
} // namespace cddl_left

namespace cddl_right {
struct Thing {
    std::string value;
};
} // namespace cddl_right

struct CDDLNameCollisionRoot {
    cddl_left::Thing  left;
    cddl_right::Thing right;
};

struct CDDLEmpty {};

#if CBOR_TAGS_HAS_NAMED_REFLECTION
struct CDDLNamedPerson {
    int         age;
    std::string name;
    std::string employer;
};

struct CDDLNamedDog {
    int         age;
    std::string name;
    float       leash_length;
};

struct CDDLNamedIdentity {
    int         age;
    std::string name;
};

struct CDDLNamedPersonWithPii {
    as_named_group<CDDLNamedPerson> pii;
};

struct CDDLNamedPersonWithIdentity {
    as_named_group<CDDLNamedIdentity> identity;
    std::string                       employer;
};

struct CDDLNamedDogWithIdentity {
    as_named_group<CDDLNamedIdentity> identity;
    float                             leash_length;
};

struct CDDLNameComponents {
    std::optional<std::string> firstName;
    std::optional<std::string> familyName;
};

struct CDDLPersonalData {
    std::optional<std::string>         displayName;
    as_named_group<CDDLNameComponents> NameComponents;
    std::optional<std::uint32_t>       age;
};

struct CDDLPersonalDataExtensible {
    std::optional<std::string>                             displayName;
    as_named_group<CDDLNameComponents>                     NameComponents;
    std::optional<std::uint32_t>                           age;
    as_named_extension<std::map<std::string, std::string>> extensions;
};

struct CDDLAccountOwner {
    std::optional<std::string> givenName;
    std::optional<std::string> familyName;
};

struct CDDLAccountLocation {
    std::string                office;
    std::string                country;
    std::optional<std::string> timezone;
};

struct CDDLAccountProfile {
    std::string                                            accountId;
    as_named_group<CDDLAccountOwner>                       owner;
    as_named_group<CDDLAccountLocation>                    location;
    std::vector<std::string>                               roles;
    std::map<std::string, std::uint32_t>                   counters;
    std::optional<bool>                                    active;
    as_named_extension<std::map<std::string, std::string>> metadata;
};

struct CDDLNamedNullablePointers {
    std::unique_ptr<std::uint64_t>      count;
    std::shared_ptr<std::string>        name;
    std::optional<std::shared_ptr<int>> maybe_count;
};

struct CDDLNamedEnumChildMap {
    CDDLTrafficLight light;
};

struct CDDLNamedEnumInlineGroup {
    CDDLTrafficLight                light;
    std::optional<CDDLSignedChoice> maybe_choice;
};

struct CDDLNamedEnumRoot {
    as_named_group<CDDLNamedEnumInlineGroup> group;
    as_named_map<CDDLNamedEnumChildMap>      child;
    std::optional<CDDLTrafficLight>          status;
};

struct CDDLNestedInlineLeaf {
    std::optional<std::string> one;
    std::optional<std::string> two;
};

struct CDDLNestedInlineMiddle {
    as_named_group<CDDLNestedInlineLeaf> leaf;
    std::string                          middle;
};

struct CDDLNestedInlineRoot {
    as_named_group<CDDLNestedInlineMiddle> middle;
    std::string                            root;
};

struct CDDLNestedMapWithRepeatedLocalName {
    int value;
};

struct CDDLRootWithNestedMapRepeatedLocalName {
    int                                              value;
    as_named_map<CDDLNestedMapWithRepeatedLocalName> child;
};

struct CDDLNamedCollisionGroup {
    int value;
};

struct CDDLNamedCollisionRoot {
    as_named_group<CDDLNamedCollisionGroup> group;
    int                                     value;
};

static_assert(detail::named_fixed_member_keys_are_unique<CDDLNamedPerson>());
static_assert(detail::named_fixed_member_keys_are_unique<CDDLPersonalDataExtensible>());
static_assert(detail::named_fixed_member_keys_are_unique<CDDLAccountProfile>());
static_assert(detail::named_fixed_member_keys_are_unique<CDDLNestedInlineRoot>());
static_assert(detail::named_fixed_member_keys_are_unique<CDDLRootWithNestedMapRepeatedLocalName>());
static_assert(!detail::named_fixed_member_keys_are_unique<CDDLNamedCollisionRoot>());

struct CDDLBorrowedExtensionRoot {
    int                                                         id;
    as_named_extension<std::map<std::string_view, std::string>> extensions;
};

struct CDDLOwningExtensionRoot {
    int                                                    id;
    as_named_extension<std::map<std::string, std::string>> extensions;
};

struct CDDLGroupedExtension {
    as_named_extension<std::map<std::string, std::string>> extensions;
};

struct CDDLGroupedExtensionRoot {
    int                                  id;
    as_named_group<CDDLGroupedExtension> group;
};

struct CDDLNestedMapScopedExtensionChild {
    int                                                    childId;
    as_named_extension<std::map<std::string, std::string>> extensions;
};

struct CDDLNestedMapScopedExtensionRoot {
    int                                                    rootId;
    as_named_map<CDDLNestedMapScopedExtensionChild>        child;
    as_named_extension<std::map<std::string, std::string>> extensions;
};

struct CDDLRootWithTwoExtensions {
    as_named_extension<std::map<std::string, std::string>> first;
    as_named_extension<std::map<std::string, std::string>> second;
};

static_assert(detail::named_flattened_extension_count<CDDLOwningExtensionRoot>() == 1U);
static_assert(detail::named_flattened_extension_count<CDDLGroupedExtensionRoot>() == 1U);
static_assert(detail::named_flattened_extension_count<CDDLNestedMapScopedExtensionRoot>() == 1U);
static_assert(detail::named_flattened_extension_count<CDDLRootWithTwoExtensions>() == 2U);

#endif
} // namespace cbor_tags_test_cddl

using namespace cbor_tags_test_cddl;

struct B129058 {
    static constexpr std::uint64_t cbor_tag = 140;
    std::vector<std::byte>         a;
    std::map<int, std::string>     b;
};

struct C122999 {
    static_tag<141>        cbor_tag;
    int                    a;
    std::string            b;
    std::optional<B129058> c;
};

struct A13213 {
    static constexpr std::uint64_t cbor_tag = 139;
    int                            pos;
    int                            nega;
    std::string                    c;
    std::vector<std::byte>         d;
    std::map<int, std::string>     e;
    struct B1212 {
        static_tag<140> cbor_tag;
        struct B1313 {
            static_tag<142> cbor_tag;

            int         a;
            std::string b;
        } b1313;
        int a;
    } f;
    float16_t      g;
    float          h;
    double         i;
    bool           j;
    std::nullptr_t k;
};

struct A42121 {
    uint32_t                       a1;
    int                            a;
    double                         b;
    float                          c;
    bool                           d;
    std::string                    e;
    std::vector<std::byte>         f;
    std::map<int, std::string>     g;
    std::variant<int, std::string> h;
    std::optional<int>             i;
    B129058                        j;
    C122999                        k;
};

TEST_CASE("CDDL extension") {
    fmt::memory_buffer buffer;
    cddl_schema_to<A42121>(buffer);
    CBOR_TAGS_TEST_LOG("CDDL: \n{}\n", fmt::to_string(buffer));
    CHECK(substrings_in(fmt::to_string(buffer), "uint,\n", "int / tstr,\n"));
}

TEST_CASE("CDDL aggregate tagged") {
    fmt::memory_buffer buffer;
    cddl_schema_to<A13213>(buffer, {.row_options = {.format_by_rows = true}});
    CBOR_TAGS_TEST_LOG("CDDL: \n{}\n", fmt::to_string(buffer));
}

TEST_CASE("CDDL emits RFC 8610 shapes for aggregate arrays and tag payloads") {
    CHECK_EQ(cddl_schema_inline<CDDLPlainTwo>(), "CDDLPlainTwo = [int, tstr]");
    CHECK_EQ(cddl_schema_inline<CDDLTaggedOne>(), "CDDLTaggedOne = #6.42(tstr)");
    CHECK_EQ(cddl_schema_inline<CDDLInlineTaggedOne>(), "CDDLInlineTaggedOne = #6.43(tstr)");
    CHECK_EQ(cddl_schema_inline<CDDLDynamicTaggedOne>(), "CDDLDynamicTaggedOne = #6(int)");
}

TEST_CASE("CDDL emits typed containers and registers nested definitions once") {
    const auto schema = cddl_schema_inline<CDDLContainers>();
    CBOR_TAGS_TEST_LOG("CDDL containers: \n{}\n", schema);

    CHECK(substrings_in(schema, "CDDLContainers = [[* int], {* int => tstr}, [2*2 int], CDDLPlainTwo / null, int / CDDLPlainTwo]",
                        "CDDLPlainTwo = [int, tstr]"));
    CHECK_EQ(count_occurrences(schema, "CDDLPlainTwo = [int, tstr]"), 1);
    CHECK_EQ(schema.find("array"), std::string::npos);
    CHECK_EQ(schema.find("map"), std::string::npos);
}

TEST_CASE("CDDL emits RFC 8746 typed-array extension shapes") {
    namespace rfc8746 = cbor::tags::ext::rfc8746;

    check_cddl_typed_array_tag<rfc8746::typed_array<std::uint8_t>>(64);
    check_cddl_typed_array_tag<rfc8746::typed_array_be<std::uint16_t>>(65);
    check_cddl_typed_array_tag<rfc8746::typed_array_be<std::uint32_t>>(66);
    check_cddl_typed_array_tag<rfc8746::typed_array_be<std::uint64_t>>(67);
    check_cddl_typed_array_tag<rfc8746::typed_array<rfc8746::uint8_clamped>>(68);
    check_cddl_typed_array_tag<rfc8746::typed_array<std::uint16_t>>(69);
    check_cddl_typed_array_tag<rfc8746::typed_array<std::uint32_t>>(70);
    check_cddl_typed_array_tag<rfc8746::typed_array<std::uint64_t>>(71);
    check_cddl_typed_array_tag<rfc8746::typed_array<std::int8_t>>(72);
    check_cddl_typed_array_tag<rfc8746::typed_array_be<std::int16_t>>(73);
    check_cddl_typed_array_tag<rfc8746::typed_array_be<std::int32_t>>(74);
    check_cddl_typed_array_tag<rfc8746::typed_array_be<std::int64_t>>(75);
    check_cddl_typed_array_tag<rfc8746::typed_array<std::int16_t>>(77);
    CHECK_EQ(cddl_schema_inline<rfc8746::typed_array<std::int32_t>>(), "root = #6.78(bstr)");
    check_cddl_typed_array_tag<rfc8746::typed_array<std::int64_t>>(79);
    check_cddl_typed_array_tag<rfc8746::typed_array_be<float16_t>>(80);
    check_cddl_typed_array_tag<rfc8746::typed_array_be<float>>(81);
    check_cddl_typed_array_tag<rfc8746::typed_array_be<double>>(82);
    check_cddl_typed_array_tag<rfc8746::typed_array_be<rfc8746::float128_t>>(83);
    check_cddl_typed_array_tag<rfc8746::typed_array<float16_t>>(84);
    check_cddl_typed_array_tag<rfc8746::typed_array<float>>(85);
    check_cddl_typed_array_tag<rfc8746::typed_array<double>>(86);
    check_cddl_typed_array_tag<rfc8746::typed_array<rfc8746::float128_t>>(87);

    CHECK_EQ(cddl_schema_inline<rfc8746::typed_array_view_be<float>>(), "root = #6.81(bstr)");
    CHECK_EQ(cddl_schema_inline<rfc8746::typed_array_ref<std::int32_t>>(), "root = #6.78(bstr)");
    CHECK_EQ(cddl_schema_inline<rfc8746::homogeneous_array<std::vector<int>>>(), "root = #6.41([* int])");

    using row_major    = rfc8746::multi_dimensional_array<std::vector<std::uint64_t>, rfc8746::typed_array<std::uint16_t>>;
    using column_major = rfc8746::multi_dimensional_column_major_array<std::vector<std::uint64_t>, rfc8746::typed_array<std::uint16_t>>;
    CHECK_EQ(cddl_schema_inline<row_major>(), "root = #6.40([[* uint], #6.69(bstr)])");
    CHECK_EQ(cddl_schema_inline<column_major>(), "root = #6.1040([[* uint], #6.69(bstr)])");

    CHECK_EQ(cddl_schema_inline<std::variant<rfc8746::typed_array<std::int32_t>, rfc8746::typed_array_be<double>>>(),
             "root = #6.78(bstr) / #6.82(bstr)");
    CHECK_EQ(cddl_schema_inline<CDDLTypedArrays>(),
             "CDDLTypedArrays = [#6.78(bstr), #6.82(bstr), #6.41([* uint]), #6.40([[* uint], #6.69(bstr)])]");
    CHECK_EQ(cddl_schema_with_options<rfc8746::typed_array<std::int32_t>>(CDDLOptions{.row_options = {}, .root_name = "samples"}),
             "samples = #6.78(bstr)");

    const auto row_schema = cddl_schema_with_options<CDDLTypedArrays>({});
    CHECK(substrings_in(row_schema, "#6.78(bstr)", "#6.82(bstr)", "#6.41([* uint])", "#6.40([[* uint], #6.69(bstr)])"));
}

TEST_CASE("CDDL does not infer RFC 8746 wrappers by structural member names") {
    const auto schema = cddl_schema_inline<CDDLLooksLikeTypedArrayRoot>();
    CBOR_TAGS_TEST_LOG("CDDL structural lookalike: \n{}\n", schema);

    CHECK(substrings_in(schema, "CDDLLooksLikeTypedArrayRoot = [CDDLLooksLikeTypedArray, int]", "CDDLLooksLikeTypedArray = int"));
    CHECK_EQ(schema.find("#6.999"), std::string::npos);
}

TEST_CASE("CDDL emits nullable pointer shapes for the smart pointer codec") {
    CHECK_EQ(cddl_schema_inline<std::unique_ptr<int>>(), "root = [0] / [1, int]");
    CHECK_EQ(cddl_schema_inline<std::shared_ptr<std::string>>(), "root = [0] / [1, tstr]");
    CHECK_EQ(cddl_schema_inline<std::unique_ptr<std::variant<int, std::string>>>(), "root = [0] / [1, (int / tstr)]");
    CHECK_EQ(cddl_schema_inline<std::optional<std::shared_ptr<int>>>(), "root = [0] / [1, int] / null");
    CHECK_EQ(cddl_schema_inline<CDDLVariantWithDeepValue>(), "root = " + repeated_vector_cddl("int", 10U) + " / tstr");

    const auto schema = cddl_schema_inline<CDDLNullablePointers>();
    CBOR_TAGS_TEST_LOG("CDDL nullable pointers: \n{}\n", schema);

    CHECK(substrings_in(schema, "CDDLNullablePointers = [[0] / [1, uint], [0] / [1, tstr], [* ([0] / [1, CDDLPlainTwo])]]",
                        "CDDLPlainTwo = [int, tstr]"));
    CHECK_EQ(count_occurrences(schema, "CDDLPlainTwo = [int, tstr]"), 1);
}

TEST_CASE("CDDL supports recursive aggregate containers") {
    CHECK_EQ(cddl_schema_inline<CDDLRecursiveNode>(), "CDDLRecursiveNode = [* CDDLRecursiveNode]");
    CHECK_EQ(cddl_schema_inline<CDDLNullablePointerRecursiveNode>(),
             "CDDLNullablePointerRecursiveNode = [uint, [0] / [1, CDDLNullablePointerRecursiveNode]]");

    const auto recursive_variant_schema = cddl_schema_inline<CDDLVariantWithRecursiveValue>();
    CHECK(substrings_in(recursive_variant_schema, "root = CDDLRecursiveNode / tstr", "CDDLRecursiveNode = [* CDDLRecursiveNode]"));
    CHECK_EQ(count_occurrences(recursive_variant_schema, "CDDLRecursiveNode = [* CDDLRecursiveNode]"), 1);

    fmt::memory_buffer inline_buffer;
    cddl_schema_to<CDDLRecursiveNode>(inline_buffer, {.row_options = {.format_by_rows = false}, .always_inline = true});
    CHECK_EQ(fmt::to_string(inline_buffer), "CDDLRecursiveNode = [* CDDLRecursiveNode]");
}

TEST_CASE("CDDL gives colliding C++ short names distinct rule names") {
    const auto schema = cddl_schema_inline<CDDLNameCollisionRoot>();
    CBOR_TAGS_TEST_LOG("CDDL collision: \n{}\n", schema);

    CHECK_EQ(schema.find("CDDLNameCollisionRoot = [Thing, Thing]"), std::string::npos);
    CHECK(substrings_in(schema, "Thing = int", " = tstr"));
}

TEST_CASE("CDDL groups choices in map keys and repeated item positions") {
    CHECK_EQ(cddl_schema_inline<CDDLNestedChoices>(),
             "CDDLNestedChoices = [[* (CDDLNestedLeaf / null)], {* (int / tstr) => (CDDLNestedTagged / null)}]\n"
             "CDDLNestedTagged = #6.9(CDDLNestedLeaf)\n"
             "CDDLNestedLeaf = [int, tstr]");
}

TEST_CASE("CDDL supports root expressions for anonymous schema roots") {
    fmt::memory_buffer vector_buffer;
    cddl_schema_to<std::vector<int>>(vector_buffer, {.row_options = {.format_by_rows = false}});
    CHECK_EQ(fmt::to_string(vector_buffer), "root = [* int]");

    fmt::memory_buffer tuple_buffer;
    cddl_schema_to<std::tuple<int, std::string>>(tuple_buffer, {.row_options = {.format_by_rows = false}});
    CHECK_EQ(fmt::to_string(tuple_buffer), "root = [int, tstr]");

    fmt::memory_buffer tag_buffer;
    cddl_schema_to<static_tag<7>>(tag_buffer, {.row_options = {.format_by_rows = false}});
    CHECK_EQ(fmt::to_string(tag_buffer), "root = #6.7(any)");

    CHECK_EQ(cddl_schema_inline<simple>(), "root = #7.<0..23 / 32..255>");

    static_assert(detail::is_empty_cddl_aggregate_v<CDDLEmpty>);
    static_assert(detail::is_cddl_tag_only_tuple_v<std::tuple<static_tag<7>>>);
    static_assert(!detail::is_cddl_tag_only_tuple_v<std::tuple<static_tag<7>, int>>);
}

TEST_CASE("CDDL supports catch-all header roots") {
    CHECK_EQ(cddl_schema_inline<as_array_any>(), "root = [* any]");
    CHECK_EQ(cddl_schema_inline<as_map_any>(), "root = {* any => any}");
    CHECK_EQ(cddl_schema_inline<as_tag_any>(), "root = #6(any)");
}

TEST_CASE("CDDL supports always_inline and enum underlying integer shapes") {
    fmt::memory_buffer inline_buffer;
    cddl_schema_to<CDDLContainers>(inline_buffer, {.row_options = {.format_by_rows = false}, .always_inline = true});
    const auto inline_schema = fmt::to_string(inline_buffer);
    CHECK(substrings_in(inline_schema, "[int, tstr] / null", "int / [int, tstr]"));
    CHECK_EQ(inline_schema.find("CDDLPlainTwo ="), std::string::npos);

    CHECK_EQ(cddl_schema_inline<CDDLEnums>(), "CDDLEnums = [uint, int]");
}

#if CBOR_TAGS_HAS_STD_REFLECTION || CBOR_TAGS_HAS_MAGIC_ENUM_NAMES
TEST_CASE("CDDL can emit named enum choices") {
    fmt::memory_buffer root;
    cddl_schema_to<CDDLTrafficLight>(
        root, {.row_options = {.format_by_rows = false}, .root_name = "traffic-light", .enum_mode = CDDLEnumMode::named_values});
    CHECK_EQ(fmt::to_string(root), "traffic_light = &(red: 1, yellow: 2, green: 4)");

    fmt::memory_buffer inline_root;
    cddl_schema_to<CDDLTrafficLight>(
        inline_root, {.row_options = {.format_by_rows = false}, .always_inline = true, .enum_mode = CDDLEnumMode::named_values});
    CHECK_EQ(fmt::to_string(inline_root), "CDDLTrafficLight = &(red: 1, yellow: 2, green: 4)");

    fmt::memory_buffer unordered;
    cddl_schema_to<CDDLUnorderedChoice>(
        unordered, {.row_options = {.format_by_rows = false}, .always_inline = true, .enum_mode = CDDLEnumMode::named_values});
    CHECK_EQ(fmt::to_string(unordered), "CDDLUnorderedChoice = &(negative: -1, low: 1, high: 5)");
}

TEST_CASE("CDDL reuses named enum definitions inside aggregate schemas") {
    fmt::memory_buffer buffer;
    cddl_schema_to<CDDLEnumNames>(buffer, {.row_options = {.format_by_rows = false}, .enum_mode = CDDLEnumMode::named_values});
    const auto schema = fmt::to_string(buffer);

    CHECK_EQ(schema, "CDDLEnumNames = [CDDLTrafficLight, CDDLSignedChoice / null, CDDLTrafficLight / int, "
                     "{* CDDLTrafficLight => tstr}, uint]\n"
                     "CDDLSignedChoice = &(negative: -2, zero: 0, positive: 3)\n"
                     "CDDLTrafficLight = &(red: 1, yellow: 2, green: 4)");
    CHECK_EQ(count_occurrences(schema, "\nCDDLTrafficLight ="), 1);
    CHECK_EQ(count_occurrences(schema, "\nCDDLSignedChoice ="), 1);
}

TEST_CASE("CDDL reuses named enum definitions through nested aggregate schemas") {
    fmt::memory_buffer buffer;
    cddl_schema_to<CDDLEnumNestedRoot>(buffer, {.row_options = {.format_by_rows = false}, .enum_mode = CDDLEnumMode::named_values});
    const auto schema = fmt::to_string(buffer);

    CHECK(substrings_in(schema, "CDDLEnumNestedRoot = [CDDLEnumNestedMiddle, [* CDDLTrafficLight]]",
                        "CDDLEnumNestedMiddle = [CDDLEnumNestedLeaf, CDDLSignedChoice / null]", "CDDLEnumNestedLeaf = CDDLTrafficLight",
                        "CDDLSignedChoice = &(negative: -2, zero: 0, positive: 3)", "CDDLTrafficLight = &(red: 1, yellow: 2, green: 4)"));
    CHECK_EQ(count_occurrences(schema, "\nCDDLTrafficLight ="), 1);
    CHECK_EQ(count_occurrences(schema, "\nCDDLSignedChoice ="), 1);
}

TEST_CASE("CDDL keeps enum underlying shapes unless named enum mode is requested") {
    CHECK_EQ(cddl_schema_inline<CDDLTrafficLight>(), "root = uint");

    fmt::memory_buffer empty_enum;
    cddl_schema_to<CDDLUnsignedEnum>(empty_enum, {.row_options = {.format_by_rows = false}, .enum_mode = CDDLEnumMode::named_values});
    CHECK_EQ(fmt::to_string(empty_enum), "root = uint");
}

TEST_CASE("CDDL named enum backend emits all enumerators outside the magic_enum default range") {
    fmt::memory_buffer buffer;
    cddl_schema_to<CDDLWideMagicEnum>(buffer, {.row_options = {.format_by_rows = false}, .enum_mode = CDDLEnumMode::named_values});
    CHECK_EQ(fmt::to_string(buffer), "CDDLWideMagicEnum = &(low: 1, high: 1000)");
}
#endif

#if !CBOR_TAGS_HAS_STD_REFLECTION && !CBOR_TAGS_HAS_MAGIC_ENUM_NAMES
TEST_CASE("CDDL named enum mode falls back without enum-name backend") {
    fmt::memory_buffer buffer;
    cddl_schema_to<CDDLTrafficLight>(buffer, {.row_options = {.format_by_rows = false}, .enum_mode = CDDLEnumMode::named_values});
    CHECK_EQ(fmt::to_string(buffer), "root = uint");
}
#endif

#if CBOR_TAGS_HAS_NAMED_REFLECTION
TEST_CASE("named-map CDDL covers RFC 8610 map and group examples") {
    fmt::memory_buffer direct_map;
    cddl_schema_to<as_named_map<CDDLNamedPerson>>(direct_map, {.row_options = {.format_by_rows = false}, .root_name = "person"});
    CHECK_EQ(fmt::to_string(direct_map), "person = {age: int, name: tstr, employer: tstr}");

    fmt::memory_buffer direct_dog_map;
    cddl_schema_to<as_named_map<CDDLNamedDog>>(direct_dog_map, {.row_options = {.format_by_rows = false}, .root_name = "dog"});
    CHECK_EQ(fmt::to_string(direct_dog_map), "dog = {age: int, name: tstr, leash_length: float32}");

    fmt::memory_buffer basic_group;
    cddl_schema_to<as_named_group<CDDLNamedPerson>>(basic_group, {.row_options = {.format_by_rows = false}, .root_name = "pii"});
    CHECK_EQ(fmt::to_string(basic_group), "pii = (age: int, name: tstr, employer: tstr)");

    fmt::memory_buffer group_by_name;
    cddl_schema_to<as_named_map<CDDLNamedPersonWithPii>>(group_by_name, {.row_options = {.format_by_rows = false}, .root_name = "person"});
    CHECK_EQ(fmt::to_string(group_by_name), "person = {pii}\npii = (age: int, name: tstr, employer: tstr)");

    fmt::memory_buffer parenthesized_group;
    cddl_schema_to<as_named_map<CDDLNamedPersonWithPii>>(
        parenthesized_group, {.row_options = {.format_by_rows = false}, .always_inline = true, .root_name = "person"});
    CHECK_EQ(fmt::to_string(parenthesized_group), "person = {(age: int, name: tstr, employer: tstr)}");
}

TEST_CASE("named-map CDDL covers RFC 8610 group factorization and personal data examples") {
    fmt::memory_buffer person;
    cddl_schema_to<as_named_map<CDDLNamedPersonWithIdentity>>(person, {.row_options = {.format_by_rows = false}, .root_name = "person"});
    CHECK_EQ(fmt::to_string(person), "person = {identity, employer: tstr}\nidentity = (age: int, name: tstr)");

    fmt::memory_buffer dog;
    cddl_schema_to<as_named_map<CDDLNamedDogWithIdentity>>(dog, {.row_options = {.format_by_rows = false}, .root_name = "dog"});
    CHECK_EQ(fmt::to_string(dog), "dog = {identity, leash_length: float32}\nidentity = (age: int, name: tstr)");

    fmt::memory_buffer personal_data;
    cddl_schema_to<as_named_map<CDDLPersonalData>>(personal_data, {.row_options = {.format_by_rows = false}, .root_name = "PersonalData"});
    CHECK_EQ(fmt::to_string(personal_data), "PersonalData = {? displayName: tstr, NameComponents, ? age: uint}\n"
                                            "NameComponents = (? firstName: tstr, ? familyName: tstr)");

    fmt::memory_buffer extensible;
    cddl_schema_to<as_named_map<CDDLPersonalDataExtensible>>(extensible,
                                                             {.row_options = {.format_by_rows = false}, .root_name = "PersonalData"});
    CHECK_EQ(fmt::to_string(extensible), "PersonalData = {? displayName: tstr, NameComponents, ? age: uint, * tstr => tstr}\n"
                                         "NameComponents = (? firstName: tstr, ? familyName: tstr)");
}

TEST_CASE("named-map CDDL covers a larger grouped profile") {
    fmt::memory_buffer account;
    cddl_schema_to<as_named_map<CDDLAccountProfile>>(account, {.row_options = {.format_by_rows = false}, .root_name = "AccountProfile"});
    CHECK_EQ(fmt::to_string(account),
             "AccountProfile = {accountId: tstr, owner, location, roles: [* tstr], counters: {* tstr => uint}, ? active: bool, "
             "* tstr => tstr}\n"
             "location = (office: tstr, country: tstr, ? timezone: tstr)\n"
             "owner = (? givenName: tstr, ? familyName: tstr)");
}

TEST_CASE("named-map CDDL keeps nullable pointer fields required unless optional") {
    fmt::memory_buffer buffer;
    cddl_schema_to<as_named_map<CDDLNamedNullablePointers>>(buffer, {.row_options = {.format_by_rows = false}, .root_name = "Pointers"});
    CHECK_EQ(fmt::to_string(buffer), "Pointers = {count: [0] / [1, uint], name: [0] / [1, tstr], ? maybe_count: [0] / [1, int]}");
}

TEST_CASE("named-map CDDL indents nested inline named groups by depth") {
    fmt::memory_buffer buffer;
    cddl_schema_to<as_named_map<CDDLNestedInlineRoot>>(
        buffer, {.row_options = {.format_by_rows = true}, .always_inline = true, .root_name = "Root"});
    CHECK_EQ(fmt::to_string(buffer), "Root = {\n"
                                     "  (\n"
                                     "    (\n"
                                     "      ? one: tstr,\n"
                                     "      ? two: tstr\n"
                                     "    ),\n"
                                     "    middle: tstr\n"
                                     "  ),\n"
                                     "  root: tstr\n"
                                     "}");
}

TEST_CASE("named-map CDDL scopes repeated local names inside nested named maps") {
    fmt::memory_buffer buffer;
    cddl_schema_to<as_named_map<CDDLRootWithNestedMapRepeatedLocalName>>(buffer,
                                                                         {.row_options = {.format_by_rows = false}, .root_name = "Root"});
    CHECK_EQ(fmt::to_string(buffer), "Root = {value: int, child: CDDLNestedMapWithRepeatedLocalName}\n"
                                     "CDDLNestedMapWithRepeatedLocalName = {value: int}");
}

#if CBOR_TAGS_HAS_STD_REFLECTION || CBOR_TAGS_HAS_MAGIC_ENUM_NAMES
TEST_CASE("named-map CDDL reuses named enum definitions through nested named maps and groups") {
    fmt::memory_buffer buffer;
    cddl_schema_to<as_named_map<CDDLNamedEnumRoot>>(buffer,
                                                    {.row_options = {.format_by_rows = false}, .enum_mode = CDDLEnumMode::named_values});
    const auto schema = fmt::to_string(buffer);

    CHECK(substrings_in(schema, "CDDLNamedEnumRoot = {group, child: CDDLNamedEnumChildMap, ? status: CDDLTrafficLight}",
                        "group = (light: CDDLTrafficLight, ? maybe_choice: CDDLSignedChoice)",
                        "CDDLNamedEnumChildMap = {light: CDDLTrafficLight}", "CDDLSignedChoice = &(negative: -2, zero: 0, positive: 3)",
                        "CDDLTrafficLight = &(red: 1, yellow: 2, green: 4)"));
    CHECK_EQ(count_occurrences(schema, "\nCDDLTrafficLight ="), 1);
    CHECK_EQ(count_occurrences(schema, "\nCDDLSignedChoice ="), 1);
    CHECK_EQ(count_occurrences(schema, "\nCDDLNamedEnumChildMap ="), 1);
    CHECK_EQ(count_occurrences(schema, "\ngroup ="), 1);
}
#endif

TEST_CASE("nested named groups roundtrip with the C++20 named reflection backend") {
    CDDLNestedInlineRoot input{
        .middle = as_named_group<CDDLNestedInlineMiddle>{CDDLNestedInlineMiddle{
            .leaf   = as_named_group<CDDLNestedInlineLeaf>{CDDLNestedInlineLeaf{.one = std::string{"a"}, .two = std::string{"b"}}},
            .middle = "m"}},
        .root   = "r"};

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(as_named_map{input}));

    CDDLNestedInlineRoot decoded{};
    auto                 dec = make_decoder(buffer);
    REQUIRE(dec(as_named_map{decoded}));
    REQUIRE(decoded.middle.value_.leaf.value_.one.has_value());
    REQUIRE(decoded.middle.value_.leaf.value_.two.has_value());
    CHECK_EQ(*decoded.middle.value_.leaf.value_.one, "a");
    CHECK_EQ(*decoded.middle.value_.leaf.value_.two, "b");
    CHECK_EQ(decoded.middle.value_.middle, "m");
    CHECK_EQ(decoded.root, "r");
}

TEST_CASE("named-map CDDL keeps table examples typed") {
    fmt::memory_buffer square_roots;
    cddl_schema_to<std::map<int, float>>(square_roots, {.row_options = {.format_by_rows = false}, .root_name = "square_roots"});
    CHECK_EQ(fmt::to_string(square_roots), "square_roots = {* int => float32}");

    fmt::memory_buffer to_string_table;
    cddl_schema_to<std::map<std::variant<int, float>, std::string>>(to_string_table,
                                                                    {.row_options = {.format_by_rows = false}, .root_name = "tostring"});
    CHECK_EQ(fmt::to_string(to_string_table), "tostring = {* (int / float32) => tstr}");
}

TEST_CASE("named-map codec roundtrips and accepts unordered maps") {
    CDDLNamedPerson        input{.age = 42, .name = "Ada", .employer = "AcmeCo"};
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(as_named_map{input}));
    CHECK_EQ(to_hex(buffer), "a363616765182a646e616d656341646168656d706c6f7965726641636d65436f");

    auto            unordered = to_bytes("a3646e616d656341646168656d706c6f7965726641636d65436f63616765182a");
    CDDLNamedPerson decoded{};
    auto            dec = make_decoder(unordered);
    REQUIRE(dec(as_named_map{decoded}));
    CHECK_EQ(decoded.age, 42);
    CHECK_EQ(decoded.name, "Ada");
    CHECK_EQ(decoded.employer, "AcmeCo");

    auto            indefinite = to_bytes("bf63616765182a646e616d656341646168656d706c6f7965726641636d65436fff");
    CDDLNamedPerson indefinite_decoded{};
    auto            indefinite_dec = make_decoder(indefinite);
    REQUIRE(indefinite_dec(as_named_map{indefinite_decoded}));
    CHECK_EQ(indefinite_decoded.age, 42);
    CHECK_EQ(indefinite_decoded.name, "Ada");
    CHECK_EQ(indefinite_decoded.employer, "AcmeCo");
}

TEST_CASE("named-map codec rejects chunked indefinite text-string keys") {
    auto            input = to_bytes("a37f63616765ff182a7f646e616d65ff634164617f68656d706c6f796572ff6641636d65436f");
    CDDLNamedPerson decoded{};
    auto            dec = make_decoder(input);
    CHECK_FALSE(dec(as_named_map{decoded}));
}

TEST_CASE("named-map codec rejects borrowed extension keys from non-contiguous inputs") {
    auto input_vector = to_bytes("a262696401686e69636b6e616d6563616365");
    auto input        = std::deque<std::byte>(input_vector.begin(), input_vector.end());

    CDDLBorrowedExtensionRoot decoded{};
    auto                      dec    = make_decoder(input);
    auto                      result = dec(as_named_map{decoded});
    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::contiguous_view_on_non_contiguous_data);
}

TEST_CASE("named-map codec copies owning extension keys from non-contiguous inputs") {
    auto input_vector = to_bytes("a262696401686e69636b6e616d6563616365");
    auto input        = std::deque<std::byte>(input_vector.begin(), input_vector.end());

    CDDLOwningExtensionRoot decoded{};
    auto                    dec    = make_decoder(input);
    auto                    result = dec(as_named_map{decoded});
    REQUIRE(result);
    CHECK_EQ(decoded.id, 1);
    REQUIRE(decoded.extensions.value_.contains("nickname"));
    CHECK_EQ(decoded.extensions.value_.at("nickname"), "ace");
}

TEST_CASE("named-map codec supports one root extension field") {
    CDDLOwningExtensionRoot input{.id = 7, .extensions = as_named_extension<std::map<std::string, std::string>>{{{"nickname", "ace"}}}};

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(as_named_map{input}));

    CDDLOwningExtensionRoot decoded{};
    auto                    dec = make_decoder(buffer);
    REQUIRE(dec(as_named_map{decoded}));
    CHECK_EQ(decoded.id, 7);
    REQUIRE(decoded.extensions.value_.contains("nickname"));
    CHECK_EQ(decoded.extensions.value_.at("nickname"), "ace");

    fmt::memory_buffer schema;
    cddl_schema_to<as_named_map<CDDLOwningExtensionRoot>>(schema, {.row_options = {.format_by_rows = false}, .root_name = "root_ext"});
    CHECK_NE(fmt::to_string(schema).find("root_ext = {id: int, * tstr => tstr}"), std::string::npos);
}

TEST_CASE("named-map codec supports one grouped extension field") {
    CDDLGroupedExtensionRoot input{.id    = 11,
                                   .group = as_named_group<CDDLGroupedExtension>{CDDLGroupedExtension{
                                       .extensions = as_named_extension<std::map<std::string, std::string>>{{{"nickname", "ace"}}}}}};

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(as_named_map{input}));

    CDDLGroupedExtensionRoot decoded{};
    auto                     dec = make_decoder(buffer);
    REQUIRE(dec(as_named_map{decoded}));
    CHECK_EQ(decoded.id, 11);
    REQUIRE(decoded.group.value_.extensions.value_.contains("nickname"));
    CHECK_EQ(decoded.group.value_.extensions.value_.at("nickname"), "ace");

    fmt::memory_buffer schema;
    cddl_schema_to<as_named_map<CDDLGroupedExtensionRoot>>(schema, {.row_options = {.format_by_rows = false}, .root_name = "group_ext"});
    const auto schema_text = fmt::to_string(schema);
    CHECK_NE(schema_text.find("group_ext = {id: int,"), std::string::npos);
    CHECK_NE(schema_text.find("* tstr => tstr"), std::string::npos);
}

TEST_CASE("named-map codec scopes nested map extensions") {
    CDDLNestedMapScopedExtensionChild child{.childId = 2,
                                            .extensions =
                                                as_named_extension<std::map<std::string, std::string>>{{{"childExtra", "inside"}}}};
    CDDLNestedMapScopedExtensionRoot  input{.rootId = 1,
                                            .child  = as_named_map{child},
                                            .extensions =
                                               as_named_extension<std::map<std::string, std::string>>{{{"rootExtra", "outside"}}}};

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(as_named_map{input}));

    CDDLNestedMapScopedExtensionChild decoded_child{};
    CDDLNestedMapScopedExtensionRoot  decoded{.rootId = 0, .child = as_named_map{decoded_child}};
    auto                              dec = make_decoder(buffer);
    REQUIRE(dec(as_named_map{decoded}));
    CHECK_EQ(decoded.rootId, 1);
    CHECK_EQ(decoded_child.childId, 2);
    REQUIRE(decoded.extensions.value_.contains("rootExtra"));
    CHECK_EQ(decoded.extensions.value_.at("rootExtra"), "outside");
    REQUIRE(decoded_child.extensions.value_.contains("childExtra"));
    CHECK_EQ(decoded_child.extensions.value_.at("childExtra"), "inside");

    fmt::memory_buffer schema;
    cddl_schema_to<as_named_map<CDDLNestedMapScopedExtensionRoot>>(
        schema, {.row_options = {.format_by_rows = false}, .root_name = "nested_scoped"});
    const auto schema_text = fmt::to_string(schema);
    CHECK_NE(schema_text.find("nested_scoped = {rootId: int, child:"), std::string::npos);
    CHECK_NE(schema_text.find("* tstr => tstr"), std::string::npos);
}

TEST_CASE("named-map codec handles nested named groups with unique flattened keys") {
    CDDLNestedInlineRoot input{.middle = as_named_group<CDDLNestedInlineMiddle>{CDDLNestedInlineMiddle{
                                   .leaf   = as_named_group<CDDLNestedInlineLeaf>{CDDLNestedInlineLeaf{
                                         .one = std::string{"first"},
                                         .two = std::string{"second"},
                                   }},
                                   .middle = std::string{"middle"},
                               }},
                               .root   = std::string{"root"}};

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(as_named_map{input}));

    CDDLNestedInlineRoot decoded{};
    auto                 dec = make_decoder(buffer);
    REQUIRE(dec(as_named_map{decoded}));
    REQUIRE(decoded.middle.value_.leaf.value_.one.has_value());
    CHECK_EQ(*decoded.middle.value_.leaf.value_.one, "first");
    REQUIRE(decoded.middle.value_.leaf.value_.two.has_value());
    CHECK_EQ(*decoded.middle.value_.leaf.value_.two, "second");
    CHECK_EQ(decoded.middle.value_.middle, "middle");
    CHECK_EQ(decoded.root, "root");
}

TEST_CASE("named-map codec allows repeated local names inside nested named maps") {
    CDDLNestedMapWithRepeatedLocalName     child{.value = 9};
    CDDLRootWithNestedMapRepeatedLocalName input{.value = 7, .child = as_named_map{child}};

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(as_named_map{input}));

    CDDLNestedMapWithRepeatedLocalName     decoded_child{};
    CDDLRootWithNestedMapRepeatedLocalName decoded{.value = 0, .child = as_named_map{decoded_child}};
    auto                                   dec = make_decoder(buffer);
    REQUIRE(dec(as_named_map{decoded}));
    CHECK_EQ(decoded.value, 7);
    CHECK_EQ(decoded_child.value, 9);
}

TEST_CASE("named-map codec enforces required, duplicate, and unknown keys") {
    auto            missing_required = to_bytes("a2646e616d656341646168656d706c6f7965726641636d65436f");
    CDDLNamedPerson missing{};
    auto            missing_dec = make_decoder(missing_required);
    CHECK_FALSE(missing_dec(as_named_map{missing}));

    auto            duplicate_known = to_bytes("a463616765016361676502646e616d656341646168656d706c6f7965726641636d65436f");
    CDDLNamedPerson duplicate{};
    auto            duplicate_dec = make_decoder(duplicate_known);
    CHECK_FALSE(duplicate_dec(as_named_map{duplicate}));

    auto            unknown_key = to_bytes("a463616765182a646e616d656341646168656d706c6f7965726641636d65436f65657874726101");
    CDDLNamedPerson unknown{};
    auto            unknown_dec = make_decoder(unknown_key);
    CHECK_FALSE(unknown_dec(as_named_map{unknown}));
}

TEST_CASE("named-map codec rejects malformed named-map shapes") {
    auto            non_text_key = to_bytes("a10102");
    CDDLNamedPerson non_text{};
    auto            non_text_dec = make_decoder(non_text_key);
    CHECK_FALSE(non_text_dec(as_named_map{non_text}));

    auto                       duplicate_group_key = to_bytes("a26966697273744e616d65634164616966697273744e616d6563457665");
    CDDLPersonalDataExtensible duplicate_group{};
    auto                       duplicate_group_dec = make_decoder(duplicate_group_key);
    CHECK_FALSE(duplicate_group_dec(as_named_map{duplicate_group}));

    auto                       duplicate_extension_key = to_bytes("a2686e69636b6e616d6563616365686e69636b6e616d6563616461");
    CDDLPersonalDataExtensible duplicate_extension{};
    auto                       duplicate_extension_dec = make_decoder(duplicate_extension_key);
    CHECK_FALSE(duplicate_extension_dec(as_named_map{duplicate_extension}));

    auto                       null_optional = to_bytes("a16b646973706c61794e616d65f6");
    CDDLPersonalDataExtensible null_value{};
    auto                       null_value_dec = make_decoder(null_optional);
    CHECK_FALSE(null_value_dec(as_named_map{null_value}));
}

TEST_CASE("named-map codec validates required fields inside named groups") {
    auto                        missing_group_member = to_bytes("a263616765182a68656d706c6f7965726641636d65436f");
    CDDLNamedPersonWithIdentity missing{};
    auto                        missing_dec = make_decoder(missing_group_member);
    CHECK_FALSE(missing_dec(as_named_map{missing}));
}

TEST_CASE("named-map codec resets optionals and extensions before decode") {
    CDDLPersonalDataExtensible decoded{
        .displayName = std::string{"old"},
        .NameComponents =
            as_named_group<CDDLNameComponents>{CDDLNameComponents{.firstName = std::string{"old"}, .familyName = std::string{"old"}}},
        .age        = std::uint32_t{99},
        .extensions = as_named_extension<std::map<std::string, std::string>>{{{"old", "value"}}}};

    auto input = to_bytes("a16966697273744e616d6563416461");
    auto dec   = make_decoder(input);
    REQUIRE(dec(as_named_map{decoded}));

    CHECK_FALSE(decoded.displayName.has_value());
    REQUIRE(decoded.NameComponents.value_.firstName.has_value());
    CHECK_EQ(*decoded.NameComponents.value_.firstName, "Ada");
    CHECK_FALSE(decoded.NameComponents.value_.familyName.has_value());
    CHECK_FALSE(decoded.age.has_value());
    CHECK(decoded.extensions.value_.empty());
}

TEST_CASE("named-map codec handles optionals, groups, and typed extensions") {
    CDDLPersonalDataExtensible input{
        .NameComponents =
            as_named_group<CDDLNameComponents>{CDDLNameComponents{.firstName = std::string{"Ada"}, .familyName = std::nullopt}},
        .age        = 42,
        .extensions = as_named_extension<std::map<std::string, std::string>>{{{"nickname", "ace"}}}};

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(as_named_map{input}));
    CHECK_EQ(to_hex(buffer), "a36966697273744e616d656341646163616765182a686e69636b6e616d6563616365");

    CDDLPersonalDataExtensible decoded{};
    auto                       dec = make_decoder(buffer);
    REQUIRE(dec(as_named_map{decoded}));
    CHECK_FALSE(decoded.displayName.has_value());
    REQUIRE(decoded.NameComponents.value_.firstName.has_value());
    CHECK_EQ(*decoded.NameComponents.value_.firstName, "Ada");
    CHECK_FALSE(decoded.NameComponents.value_.familyName.has_value());
    REQUIRE(decoded.age.has_value());
    CHECK_EQ(*decoded.age, 42U);
    REQUIRE(decoded.extensions.value_.contains("nickname"));
    CHECK_EQ(decoded.extensions.value_.at("nickname"), "ace");
}

TEST_CASE("named-map codec handles a larger grouped profile") {
    CDDLAccountProfile input{
        .accountId = "acct-7",
        .owner = as_named_group<CDDLAccountOwner>{CDDLAccountOwner{.givenName = std::string{"Ada"}, .familyName = std::string{"Lovelace"}}},
        .location = as_named_group<CDDLAccountLocation>{CDDLAccountLocation{.office = "London", .country = "GB", .timezone = std::nullopt}},
        .roles    = {"admin", "writer"},
        .counters = {{"logins", 42}, {"projects", 3}},
        .active   = true,
        .metadata = as_named_extension<std::map<std::string, std::string>>{{{"nickname", "ace"}, {"team", "compiler"}}}};

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(as_named_map{input}));

    CDDLAccountProfile decoded{};
    auto               dec = make_decoder(buffer);
    REQUIRE(dec(as_named_map{decoded}));

    CHECK_EQ(decoded.accountId, "acct-7");
    REQUIRE(decoded.owner.value_.givenName.has_value());
    CHECK_EQ(*decoded.owner.value_.givenName, "Ada");
    REQUIRE(decoded.owner.value_.familyName.has_value());
    CHECK_EQ(*decoded.owner.value_.familyName, "Lovelace");
    CHECK_EQ(decoded.location.value_.office, "London");
    CHECK_EQ(decoded.location.value_.country, "GB");
    CHECK_FALSE(decoded.location.value_.timezone.has_value());
    REQUIRE_EQ(decoded.roles.size(), 2U);
    CHECK_EQ(decoded.roles[0], "admin");
    CHECK_EQ(decoded.roles[1], "writer");
    REQUIRE(decoded.counters.contains("logins"));
    CHECK_EQ(decoded.counters.at("logins"), 42U);
    REQUIRE(decoded.counters.contains("projects"));
    CHECK_EQ(decoded.counters.at("projects"), 3U);
    REQUIRE(decoded.active.has_value());
    CHECK(*decoded.active);
    REQUIRE(decoded.metadata.value_.contains("nickname"));
    CHECK_EQ(decoded.metadata.value_.at("nickname"), "ace");
    REQUIRE(decoded.metadata.value_.contains("team"));
    CHECK_EQ(decoded.metadata.value_.at("team"), "compiler");
}

TEST_CASE("named-map codec rejects extension keys that shadow named fields") {
    CDDLPersonalDataExtensible input{.NameComponents =
                                         as_named_group<CDDLNameComponents>{CDDLNameComponents{.firstName = std::string{"Ada"}}},
                                     .age        = 42,
                                     .extensions = as_named_extension<std::map<std::string, std::string>>{{{"age", "forty-two"}}}};

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    CHECK_FALSE(enc(as_named_map{input}));

    CDDLPersonalDataExtensible group_shadow{.NameComponents =
                                                as_named_group<CDDLNameComponents>{CDDLNameComponents{.firstName = std::string{"Ada"}}},
                                            .age        = 42,
                                            .extensions = as_named_extension<std::map<std::string, std::string>>{{{"firstName", "Eve"}}}};

    buffer.clear();
    CHECK_FALSE(enc(as_named_map{group_shadow}));
}
#endif

struct A0001 {
    uint32_t                       a1;
    negative                       aminus;
    int                            a;
    double                         b;
    float                          c;
    bool                           d;
    std::string                    e;
    std::vector<std::byte>         f;
    std::map<int, std::string>     g;
    std::variant<int, std::string> h;
    std::optional<int>             i;
    B129058                        j;
    C122999                        k;
};

TEST_CASE("CDDL no columns") {
    fmt::memory_buffer buffer;

    cddl_schema_to<A0001>(buffer, {.row_options = {.format_by_rows = false}});
    CBOR_TAGS_TEST_LOG("CDDL: \n{}\n", fmt::to_string(buffer));

    CHECK(substrings_in(fmt::to_string(buffer), "uint,", "nint,", "int / tstr", "B129058 = #6.140([bstr, {* int => tstr}])",
                        "C122999 = #6.141([int, tstr, B129058 / null])"));
}

struct A1212 {
    // static constexpr std::uint64_t cbor_tag = 139;
    int                        pos;
    int                        nega;
    std::string                c;
    std::vector<std::byte>     d;
    std::map<int, std::string> e;
    struct B1212 {
        static_tag<140> cbor_tag;
        struct B1313 {
            static_tag<142> cbor_tag;

            int         a;
            std::string b;
        } b1313;
        int a;
    } f;
    float16_t      g;
    float          h;
    double         i;
    bool           j;
    std::nullptr_t k;
};

TEST_CASE_TEMPLATE("Annotate", T, std::vector<std::byte>, std::deque<std::byte>) {
    A1212 a1212{.pos  = 1,
                .nega = -1,
                .c    = "Hello world!",
                .d    = {std::byte{'a'}, std::byte{'b'}, std::byte{'c'}, std::byte{'d'}},
                .e    = {{1, "one"}, {2, "two"}, {3, "three"}},
                .f    = {.cbor_tag = {}, .b1313 = {.cbor_tag = {}, .a = 1000000, .b = "aaaaaaaaaaaaaaaaaaaaaaaa"}, .a = 42},
                .g    = float16_t{3.14},
                .h    = 3.14f,
                .i    = 3.14,
                .j    = true,
                .k    = nullptr};

    T buffer;

    auto enc = make_encoder(buffer);
    REQUIRE(enc(a1212));

    CBOR_TAGS_TEST_LOG("CBOR: {}\n", to_hex(buffer));

    fmt::memory_buffer annotation;
    buffer_annotate(buffer, annotation);

    CBOR_TAGS_TEST_LOG("Annotation: \n{}\n", fmt::to_string(annotation));
}

TEST_CASE_TEMPLATE("Annotate map", T, std::vector<std::byte>, std::deque<std::byte>) {
    std::map<std::variant<int, std::string>, std::variant<int, std::string, std::map<std::string, double>>> map =
        {{1, "one"}, {"two", 2}, {3, "three"}, {"four", 4}, {"map", std::map<std::string, double>{{"pi", 3.14}, {"e", 2.71}}}};

    T buffer;

    auto enc = make_encoder(buffer);
    REQUIRE(enc(map));

    CBOR_TAGS_TEST_LOG("CBOR: {}\n", to_hex(buffer));

    fmt::memory_buffer annotation;
    buffer_annotate(buffer, annotation);

    CBOR_TAGS_TEST_LOG("Annotation: \n{}\n", fmt::to_string(annotation));
}

TEST_CASE("CWT annotation") {
    auto buffer =
        to_bytes("d28443a10126a104524173796d6d657472696345434453413235365850a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b770"
                 "37818636f61703a2f2f6c696768742e6578616d706c652e636f6d041a5612aeb0051a5610d9f0061a5610d9f007420b7158405427c1ff28d23fbad1f2"
                 "9c4c7c6a555e601d6fa29f9179bc3d7438bacaca5acd08c8d4d4f96131680c429a01f85951ecee743a52b9b63632c57209120e1c9e30");

    CBOR_TAGS_TEST_LOG("CBOR Web Token (CWT): {}\n", to_hex(buffer));

    fmt::memory_buffer annotation;
    buffer_annotate(buffer, annotation);

    CBOR_TAGS_TEST_LOG("Annotation: \n{}\n", fmt::to_string(annotation));
}

TEST_CASE("CWT payload map annotation") {
    auto buffer = to_bytes("a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b77037818636f61703a2f2f6c696768742e6578616d706c652e6"
                           "36f6d041a5612aeb0051a5610d9f0061a5610d9f007420b71");

    fmt::memory_buffer annotation;
    buffer_annotate(buffer, annotation);
    CBOR_TAGS_TEST_LOG("Annotation: \n{}\n", fmt::to_string(annotation));
}

TEST_CASE("CDDL adhoc tagging") {
    struct A {
        std::string a;
    };

    fmt::memory_buffer buffer;
    using namespace cbor::tags::literals;
    using tagA = std::pair<static_tag<140>, A>;
    cddl_schema_to<tagA>(buffer, {.row_options = {.format_by_rows = false}});
    CBOR_TAGS_TEST_LOG("CDDL: \n{}\n", fmt::to_string(buffer));

    CHECK(substrings_in(fmt::to_string(buffer), "#6.140(A)", "A = tstr"));
}

TEST_CASE("CDDL PRELUDE") {
    fmt::memory_buffer buffer;
    cddl_prelude_to(buffer);
    CBOR_TAGS_TEST_LOG("CDDL: \n{}\n", fmt::to_string(buffer));

    CHECK(substrings_in(fmt::to_string(buffer), "null = nil", "int = uint / nint"));
}

TEST_CASE_TEMPLATE("Diagnostic data 0", T, std::byte, uint8_t, char) {
    auto data = to_bytes<T>(
        "d28443a10126a104524173796d6d657472696345434453413235365850a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b770"
        "37818636f61703a2f2f6c696768742e6578616d706c652e636f6d041a5612aeb0051a5610d9f0061a5610d9f007420b7158405427c1ff28d23fbad1f2"
        "9c4c7c6a555e601d6fa29f9179bc3d7438bacaca5acd08c8d4d4f96131680c429a01f85951ecee743a52b9b63632c57209120e1c9e30");

    fmt::memory_buffer buffer;
    buffer_annotate(data, buffer);
    CBOR_TAGS_TEST_LOG("Annotation: \n{}\n", fmt::to_string(buffer));

    fmt::memory_buffer buffer1;
    buffer_diagnostic(data, buffer1, {});
    CBOR_TAGS_TEST_LOG("Diagnostic: \n{}\n", fmt::to_string(buffer1));
    fmt::format_to(std::back_inserter(buffer1), "\n --- \n");

    data = to_bytes<T>("a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b77037818636f61703a2f2f6c696768742e6578616d706c652e6"
                       "36f6d041a5612aeb0051a5610d9f0061a5610d9f007420b71");

    buffer_diagnostic(data, buffer1, {});
    CBOR_TAGS_TEST_LOG("map: \n{}\n", fmt::to_string(buffer1));

    CHECK(substrings_in(buffer1, "h'a10126'", "4: h'4173796d6d65747269634543445341323536'",
                        "h'"
                        "a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b77037818636f61703a2f2f6c696768742e6578616d706c652e636f"
                        "6d041a5612aeb0051a5610d9f0061a5610d9f007420b71'",
                        "h'"
                        "5427c1ff28d23fbad1f29c4c7c6a555e601d6fa29f9179bc3d7438bacaca5acd08c8d4d4f96131680c429a01f85951ecee743a52b9b63632c5"
                        "7209120e1c9e30'",
                        R"(3: "coap://light.example.com")", R"(7: h'0b71')", "6: 1443944944"));
}
