#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_integer.h"
#include "cbor_tags/cbor_reflection.h"
#include "cbor_tags/cbor_tags_config.h"
#include "cbor_tags/detail/cbor_cddl_tag_traits.h"
#include "cbor_tags/detail/cbor_extension_decode.h"
#include "cbor_tags/detail/cbor_item.h"
#include "cbor_tags/detail/cbor_pointer_traits.h"
#include "cbor_tags/detail/text_format.h"
#include "cbor_tags/detail/type_name.h"
#include "cbor_tags/extensions/cddl_traits.h"

#ifndef CBOR_TAGS_USE_MAGIC_ENUM_NAMES
#define CBOR_TAGS_USE_MAGIC_ENUM_NAMES 0
#endif

#if CBOR_TAGS_USE_MAGIC_ENUM_NAMES
#if !__has_include(<magic_enum/magic_enum.hpp>)
#error "CBOR_TAGS_USE_MAGIC_ENUM_NAMES requires magic_enum with <magic_enum/magic_enum.hpp>"
#endif
#include <magic_enum/magic_enum.hpp>
#define CBOR_TAGS_HAS_MAGIC_ENUM_NAMES 1
#else
#define CBOR_TAGS_HAS_MAGIC_ENUM_NAMES 0
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cbor_tags/cbor_concepts.h>
#include <cbor_tags/cbor_concepts_checking.h>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <stack>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cbor::tags {

namespace text = detail::text_format;

enum class AnnotationMode { no_annotation, smart };

enum class CDDLEnumMode { underlying_integer, named_values };

struct AnnotationOptions {
    bool           diagnostic_data{false};
    size_t         current_indent{0};
    size_t         offset{0};
    size_t         max_depth{std::numeric_limits<size_t>::max()};
    AnnotationMode mode{AnnotationMode::smart};
    size_t         annotation_column{61};
    size_t         indent_width{3};
    size_t         comment_indent_width{2};
    size_t         max_structure_depth{64};
    size_t         max_input_size{std::size_t{16U} * 1024U * 1024U};
    size_t         max_output_size{std::size_t{16U} * 1024U * 1024U};
};

struct CDDLOptions {
    struct RowOptions {
        bool   format_by_rows{true};
        size_t offset{2};
        size_t current_indent{0};
    } row_options;
    bool             always_inline{false};
    std::string_view root_name{};
    CDDLEnumMode     enum_mode{CDDLEnumMode::underlying_integer};
    bool             label_array_fields{false};
};

struct DiagnosticOptions {
    struct RowOptions {
        bool   format_by_rows{true};
        bool   override_array_by_columns{false};
        size_t offset{2};
        size_t current_indent{0};
    } row_options;

    bool   check_tstr_utf8{false};
    size_t max_depth{64};
    size_t current_depth{0};
};

template <typename T, typename OutputBuffer, typename Context>
auto cddl_schema_to(OutputBuffer &output_buffer, CDDLOptions = {}, Context = {});

namespace detail {

[[nodiscard]] inline std::string negative_diagnostic_text(negative value) {
    if (value.value == 0U) {
        return "-18446744073709551616";
    }
    return text::format("-{}", value.value);
}

struct CDDLContext;

template <typename T, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string ensure_cddl_definition(CDDLContext &context, CDDLOptions options, std::string_view preferred_name = {});

struct CDDLContext {
    enum class DefinitionState { visiting, done };

    struct definition_cddl_pair {
        std::pmr::string key;
        std::pmr::string name;
        std::pmr::string cddl;
        DefinitionState  state{DefinitionState::visiting};
        bool             recursive_reference{false};

        definition_cddl_pair() = default;

        definition_cddl_pair(std::pmr::string key_, std::pmr::string name_, std::pmr::string cddl_,
                             DefinitionState state_ = DefinitionState::visiting, bool recursive_reference_ = false)
            : key(std::move(key_)), name(std::move(name_)), cddl(std::move(cddl_)), state(state_),
              recursive_reference(recursive_reference_) {}
    };

    std::array<std::byte, 2000>           buffer;
    std::pmr::monotonic_buffer_resource   memory_resource{buffer.data(), buffer.size()};
    std::pmr::deque<definition_cddl_pair> definitions{&memory_resource};

    CDDLContext() = default;

    explicit CDDLContext(const CDDLContext &other) {
        for (const auto &def : other.definitions) {
            debug::println("copying definition: {} -> {}", def.name, def.cddl);
            definitions.emplace_back(std::pmr::string(def.key, &memory_resource), std::pmr::string(def.name, &memory_resource),
                                     std::pmr::string(def.cddl, &memory_resource), def.state, def.recursive_reference);
        }
    }

    template <typename T> bool contains(const T &name) const {
        const std::string_view expected{name.data(), name.size()};
        for (const auto &def : definitions) {
            const std::string_view actual_key{def.key.data(), def.key.size()};
            const std::string_view actual_name{def.name.data(), def.name.size()};
            if (actual_key == expected || actual_name == expected) {
                return true;
            }
        }
        return false;
    }

    definition_cddl_pair *find_by_key(std::string_view key) {
        for (auto &def : definitions) {
            if (std::string_view{def.key.data(), def.key.size()} == key) {
                return &def;
            }
        }
        return nullptr;
    }

    const definition_cddl_pair *find_by_key(std::string_view key) const {
        for (const auto &def : definitions) {
            if (std::string_view{def.key.data(), def.key.size()} == key) {
                return &def;
            }
        }
        return nullptr;
    }

    const definition_cddl_pair *find_by_name(std::string_view name) const {
        for (const auto &def : definitions) {
            if (std::string_view{def.name.data(), def.name.size()} == name) {
                return &def;
            }
        }
        return nullptr;
    }

    bool contains_name(std::string_view name) const {
        for (const auto &def : definitions) {
            if (std::string_view{def.name.data(), def.name.size()} == name) {
                return true;
            }
        }
        return false;
    }

    definition_cddl_pair &reserve(std::string_view key, std::string_view name) {
        definitions.emplace_back(std::pmr::string(key, &memory_resource), std::pmr::string(name, &memory_resource),
                                 std::pmr::string(&memory_resource), DefinitionState::visiting);
        return definitions.back();
    }

    bool erase_by_key(std::string_view key) {
        for (auto it = definitions.begin(); it != definitions.end(); ++it) {
            if (std::string_view{it->key.data(), it->key.size()} == key) {
                definitions.erase(it);
                return true;
            }
        }
        return false;
    }

    void clear() {
        std::destroy_at(std::addressof(definitions));
        memory_resource.release();
        std::construct_at(std::addressof(definitions), &memory_resource);
    }

    template <typename T, typename Context> void register_type(CDDLOptions options, Context context) {
        (void)context;
        (void)ensure_cddl_definition<std::remove_cvref_t<T>>(*this, options);
    }
};

using catch_all_variant = std::variant<positive, negative, as_text_any, as_bstr_any, as_array_any, as_map_any, as_tag_any, float16_t, float,
                                       double, bool, std::nullptr_t, simple>;

template <typename Iterator> void format_bytes(auto &output_buffer, Iterator begin, Iterator end, AnnotationOptions options = {}) {
    std::string indent(options.current_indent * 2, ' ');
    std::string offset(options.offset, ' ');

    const size_t bytes_per_line = options.max_depth == std::numeric_limits<size_t>::max()
                                      ? std::distance(begin, end) // no wrapping if max_depth is max
                                      : options.max_depth;

    size_t current_count = 0;
    bool   is_first_line = true;

    while (begin != end) {
        if (current_count == 0) {
            if (!is_first_line) {
                text::format_to(std::back_inserter(output_buffer), "\n");
            }
            text::format_to(std::back_inserter(output_buffer), "{}{}", indent, offset);
            is_first_line = false;
        }

        text::format_to(std::back_inserter(output_buffer), "{:02x}", static_cast<std::uint8_t>(*begin));
        ++begin;

        ++current_count;
        if (current_count >= bytes_per_line) {
            current_count = 0;
        }
    }
}

template <typename CborBuffer, typename OutputBuffer>
void buffer_annotate_smart(const CborBuffer &cbor_buffer, OutputBuffer &output_buffer, AnnotationOptions options);

} // namespace detail

template <typename T> constexpr auto getName(const T &);
template <typename T> constexpr auto getName();

template <IsVariant Variant> constexpr auto getVariantNames() {
    return detail::with_variant_alternatives<Variant>([]<typename... Ts>() {
        std::string result;
        ((result += std::string(getName<Ts>()) + " / "), ...);
        return result.substr(0, result.empty() ? 0 : (result.size() - 3));
    });
}

template <IsVariant Variant> constexpr auto getVariantNames(const Variant &) { return getVariantNames<std::remove_cvref_t<Variant>>(); }

template <IsTag T> constexpr auto getTagDef(const T &t) {
    if constexpr (HasInlineTag<T>) {
        return text::format("#6.{}", T::cbor_tag);
    } else {
        if constexpr (IsTuple<T>) {
            const auto tag = std::get<0>(t);
            return text::format("#6.{}", static_cast<std::uint64_t>(tag));
        } else {
            return text::format("#6.{}", static_cast<std::uint64_t>(t.cbor_tag));
        }
    }
}

template <typename T> constexpr auto getName(const T &) { return getName<std::remove_cvref_t<T>>(); }

template <typename T> constexpr auto getName() {
    if constexpr (IsUnsigned<T>) {
        return "uint";
    } else if constexpr (IsNegative<T>) {
        return "nint";
    } else if constexpr (IsSigned<T>) {
        return "int";
    } else if constexpr (IsTextString<T>) {
        return "tstr";
    } else if constexpr (IsBinaryString<T>) {
        return "bstr";
    } else if constexpr (IsArray<T>) {
        return "array";
    } else if constexpr (IsMap<T>) {
        return "map";
    } else if constexpr (IsTag<T>) {
        return detail::short_type_name<T>();
    } else if constexpr (IsSimple<T>) {
        if constexpr (IsBool<T>) {
            return "bool";
        } else if constexpr (IsFloat16<T>) {
            return "float16";
        } else if constexpr (IsFloat32<T>) {
            return "float32";
        } else if constexpr (IsFloat64<T>) {
            return "float64";
        } else if constexpr (IsNull<T>) {
            return "null";
        } else {
            return "simple";
        }
    } else {
        if constexpr (IsOptional<T>) {
            using value_type = typename T::value_type;
            auto name        = getName<value_type>();
            return std::string(name) + " / null";
        } else if constexpr (detail::IsNullablePointer<T>) {
            using element_type = detail::nullable_pointer_element_t<T>;
            static_assert(detail::is_supported_nullable_pointer_v<T>,
                          "CDDL nullable pointer support requires std::unique_ptr<T> with the default deleter or "
                          "std::shared_ptr<T>, and T must be non-const, non-void, and non-array");
            static_assert(std::default_initializable<element_type>,
                          "CDDL nullable pointer support requires default-initializable pointee types because pointer decode constructs T");
            auto name = getName<element_type>();
            return std::string("[0] / [1, ") + std::string(name) + "]";
        } else if constexpr (IsVariant<T>) {
            return getVariantNames<T>();
        } else {
            return detail::short_type_name<T>();
        }
    }
}

template <typename T>
concept IsReferenceWrapper = std::is_same_v<T, std::reference_wrapper<typename T::type>>;

namespace detail {

template <typename T> struct is_std_array : std::false_type {};
template <typename T, std::size_t N> struct is_std_array<std::array<T, N>> : std::true_type {
    using value_type                  = T;
    static constexpr std::size_t size = N;
};

template <typename T> struct is_std_span : std::false_type {
    static constexpr std::size_t extent = std::dynamic_extent;
};
template <typename T, std::size_t Extent> struct is_std_span<std::span<T, Extent>> : std::true_type {
    using value_type                    = T;
    static constexpr std::size_t extent = Extent;
};

template <typename T> using aggregate_tuple_t = std::remove_cvref_t<decltype(to_tuple(std::declval<T &>()))>;

template <typename T, typename...> struct first_type {
    using type = T;
};

template <typename... Ts> using first_type_t = typename first_type<Ts...>::type;

template <typename T, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_type_expr(CDDLContext &context, CDDLOptions options);
template <typename T, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string ensure_cddl_named_map_definition(CDDLContext &context, CDDLOptions options, std::string_view preferred_name = {});
template <typename T, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string ensure_cddl_named_group_definition(CDDLContext &context, CDDLOptions options, std::string_view preferred_name = {});
template <typename T, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_named_map_expr(CDDLContext &context, CDDLOptions options);
template <typename T, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_named_group_expr(CDDLContext &context, CDDLOptions options);

template <typename T>
concept CDDLScopedType = requires {
    typename cddl_scope_traits<std::remove_cvref_t<T>>::value_type;
    { cddl_scope_traits<std::remove_cvref_t<T>>::shared_pointer_mode } -> std::convertible_to<cddl_shared_pointer_mode>;
};

template <typename... Ts> struct cddl_seen_types {};

template <typename T, typename Seen> struct cddl_seen_contains;

template <typename T, typename... Seen>
struct cddl_seen_contains<T, cddl_seen_types<Seen...>> : std::bool_constant<(std::same_as<std::remove_cvref_t<T>, Seen> || ...)> {};

template <typename Seen, typename T> struct cddl_seen_append;

template <typename... Seen, typename T> struct cddl_seen_append<cddl_seen_types<Seen...>, T> {
    using type = cddl_seen_types<Seen..., std::remove_cvref_t<T>>;
};

template <typename Seen, typename T> using cddl_seen_append_t = typename cddl_seen_append<Seen, T>::type;

template <typename T, typename Seen = cddl_seen_types<>> consteval bool cddl_contains_nullable_pointer();

template <typename Tuple, typename Seen, std::size_t... Is>
consteval bool cddl_tuple_contains_nullable_pointer(std::index_sequence<Is...>) {
    return (cddl_contains_nullable_pointer<std::tuple_element_t<Is, Tuple>, Seen>() || ...);
}

template <typename T> consteval bool cddl_scoped_type_contains_nullable_pointer() {
    // Scope wrappers are root-only. Treat nested wrappers as opaque here; rendering
    // produces the user-facing diagnostic.
    return false;
}

template <typename T, typename Seen> consteval bool cddl_contains_nullable_pointer() {
    using value_type = std::remove_cvref_t<T>;
    if constexpr (IsNullablePointer<value_type>) {
        return true;
    } else if constexpr (cddl_seen_contains<value_type, Seen>::value) {
        return false;
    } else {
        using next_seen = cddl_seen_append_t<Seen, value_type>;
        if constexpr (CDDLScopedType<value_type>) {
            return cddl_scoped_type_contains_nullable_pointer<value_type>();
        } else if constexpr (CDDLHomogeneousArray<value_type>) {
            using traits = cddl_homogeneous_array_traits<value_type>;
            return cddl_contains_nullable_pointer<typename traits::array_type, next_seen>();
        } else if constexpr (CDDLMultiDimensionalArray<value_type>) {
            using traits = cddl_multi_dimensional_array_traits<value_type>;
            return cddl_contains_nullable_pointer<typename traits::dimensions_type, next_seen>() ||
                   cddl_contains_nullable_pointer<typename traits::array_type, next_seen>();
        } else if constexpr (IsAnyBoundedSizeWrapper<value_type> || IsArrayRangeWrapper<value_type> || IsOptional<value_type> ||
                             (IsArray<value_type> && !IsIndefiniteWrapper<value_type>)) {
            return cddl_contains_nullable_pointer<typename value_type::value_type, next_seen>();
        } else if constexpr (IsVariant<value_type>) {
            return detail::with_variant_alternatives<value_type>(
                []<typename... Ts>() { return (cddl_contains_nullable_pointer<Ts, next_seen>() || ...); });
        } else if constexpr (IsNamedMapWrapper<value_type>) {
            return cddl_contains_nullable_pointer<named_map_value_t<value_type>, next_seen>();
        } else if constexpr (IsNamedGroupWrapper<value_type>) {
            return cddl_contains_nullable_pointer<named_group_value_t<value_type>, next_seen>();
        } else if constexpr (IsNamedExtensionWrapper<value_type>) {
            return cddl_contains_nullable_pointer<named_extension_value_t<value_type>, next_seen>();
        } else if constexpr (IsIndefiniteWrapper<value_type>) {
            return cddl_contains_nullable_pointer<indefinite_value_t<value_type>, next_seen>();
        } else if constexpr (IsMapRangeWrapper<value_type> || IsMap<value_type>) {
            return cddl_contains_nullable_pointer<typename value_type::key_type, next_seen>() ||
                   cddl_contains_nullable_pointer<typename value_type::mapped_type, next_seen>();
        } else if constexpr (IsTuple<value_type>) {
            return cddl_tuple_contains_nullable_pointer<value_type, next_seen>(std::make_index_sequence<std::tuple_size_v<value_type>>{});
        } else if constexpr (IsAggregate<value_type>) {
            using tuple_type = aggregate_tuple_t<value_type>;
            return cddl_tuple_contains_nullable_pointer<tuple_type, next_seen>(std::make_index_sequence<std::tuple_size_v<tuple_type>>{});
        } else {
            return false;
        }
    }
}

template <typename T> consteval std::size_t cddl_nullable_pointer_alternative_count() {
    using value_type = std::remove_cvref_t<T>;
    if constexpr (IsVariant<value_type>) {
        return detail::with_variant_alternatives<value_type>(
            []<typename... Ts>() { return (std::size_t{0} + ... + cddl_nullable_pointer_alternative_count<Ts>()); });
    } else {
        return cddl_contains_nullable_pointer<value_type>() ? std::size_t{1} : std::size_t{0};
    }
}

template <typename T> consteval bool cddl_contains_unsupported_shared_graph_variant_pointer() {
    using value_type = std::remove_cvref_t<T>;
    if constexpr (cddl_is_direct_nullable_pointer_alternative<value_type>() || cddl_is_shared_graph_vector_alternative<value_type>()) {
        return false;
    } else {
        return cddl_contains_nullable_pointer<value_type>();
    }
}

constexpr bool is_cddl_id_start(char value) {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || value == '_' || value == '$' || value == '@';
}

constexpr bool is_cddl_id_continue(char value) { return is_cddl_id_start(value) || (value >= '0' && value <= '9'); }

inline bool is_valid_cddl_id(std::string_view raw) {
    if (raw.empty() || !is_cddl_id_start(raw.front())) {
        return false;
    }
    return std::ranges::all_of(raw.substr(1), is_cddl_id_continue);
}

inline bool is_cddl_prelude_rule_name(std::string_view name) {
    static constexpr std::string_view names[] = {
        "any",     "uint",         "nint",       "int",    "bstr",      "bytes",    "tstr",         "text",     "tdate",   "time",
        "number",  "biguint",      "bignint",    "bigint", "integer",   "unsigned", "decfrac",      "bigfloat", "eb64url", "eb64legacy",
        "eb16",    "encoded-cbor", "uri",        "b64url", "b64legacy", "regexp",   "mime-message", "cbor-any", "float16", "float32",
        "float64", "float16-32",   "float32-64", "float",  "false",     "true",     "bool",         "nil",      "null",    "undefined"};
    return std::ranges::find(names, name) != std::end(names);
}

inline std::string sanitize_cddl_id(std::string_view raw) {
    std::string result;
    result.reserve(raw.size());

    for (const auto value : raw) {
        result += is_cddl_id_continue(value) ? value : '_';
    }
    while (!result.empty() && result.front() == '_') {
        result.erase(result.begin());
    }
    if (result.empty()) {
        result = "type";
    }
    if (!is_cddl_id_start(result.front())) {
        result.insert(0, "type_");
    }
    return result;
}

template <cddl_shared_pointer_mode PointerMode> constexpr std::string_view cddl_scope_key_prefix() {
    if constexpr (PointerMode == cddl_shared_pointer_mode::shared_graph) {
        return "__cddl_shared_graph:";
    } else {
        return "";
    }
}

template <typename T, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable> std::string cddl_type_key() {
    return std::string(cddl_scope_key_prefix<PointerMode>()) + std::string(detail::full_type_name<std::remove_cvref_t<T>>());
}

template <typename T> std::string cddl_type_name() { return sanitize_cddl_id(detail::short_type_name<std::remove_cvref_t<T>>()); }

inline std::string quote_cddl_text(std::string_view raw) {
    std::string result = "\"";
    for (const auto value : raw) {
        switch (value) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += value; break;
        }
    }
    result += "\"";
    return result;
}

inline std::string cddl_member_key(std::string_view raw) { return is_valid_cddl_id(raw) ? std::string(raw) : quote_cddl_text(raw); }

template <typename T, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable> std::string cddl_named_map_key() {
    return "__cddl_named_map:" + cddl_type_key<T, PointerMode>();
}

template <typename T, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable> std::string cddl_named_group_key() {
    return "__cddl_named_group:" + cddl_type_key<T, PointerMode>();
}

template <typename T> std::string cddl_enum_key() { return "__cddl_enum:" + cddl_type_key<T>(); }

template <typename T, std::size_t I, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_array_field_alias_key() {
    return text::format("__cddl_array_field:{}:{}", cddl_type_key<T, PointerMode>(), I);
}

inline std::string unique_cddl_name(CDDLContext &context, std::string_view key, std::string_view preferred_name) {
    auto preferred = sanitize_cddl_id(preferred_name.empty() ? key : preferred_name);
    if (!context.contains_name(preferred)) {
        return preferred;
    }

    auto fallback = sanitize_cddl_id(key);
    if (!context.contains_name(fallback)) {
        return fallback;
    }

    for (std::size_t suffix = 2;; ++suffix) {
        auto candidate = text::format("{}_{}", fallback, suffix);
        if (!context.contains_name(candidate)) {
            return candidate;
        }
    }
}

inline std::string parenthesize_choice(std::string value) {
    if (value.find(" / ") == std::string::npos) {
        return value;
    }
    return "(" + value + ")";
}

template <typename T> std::string cddl_tagged_bstr_array_expr() {
    using value_type = std::remove_cvref_t<T>;
    return text::format("#6.{}(bstr)", cddl_tagged_bstr_array_traits<value_type>::tag);
}

template <std::size_t Min, std::size_t Max> std::string cddl_size_control(std::string_view base);

template <typename T>
concept CDDLBoundedTaggedByteStringArray = CDDLTaggedByteStringArray<T> && requires {
    { cddl_tagged_bstr_array_traits<std::remove_cvref_t<T>>::element_byte_size } -> std::convertible_to<std::uint64_t>;
};

template <typename T, std::size_t Min, std::size_t Max> std::string cddl_bounded_tagged_bstr_array_expr() {
    using value_type       = std::remove_cvref_t<T>;
    using traits           = cddl_tagged_bstr_array_traits<value_type>;
    constexpr auto element = traits::element_byte_size;
    static_assert(element > 0U, "bounded tagged byte-string array CDDL requires a non-zero element byte size");
    static_assert(element <= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()),
                  "bounded tagged byte-string array element byte size exceeds size_t");
    constexpr auto element_size = static_cast<std::size_t>(element);
    static_assert(Min <= (std::numeric_limits<std::size_t>::max() / element_size),
                  "bounded tagged byte-string array minimum byte size overflows size_t");
    static_assert(Max <= (std::numeric_limits<std::size_t>::max() / element_size),
                  "bounded tagged byte-string array maximum byte size overflows size_t");
    return text::format("#6.{}({})", traits::tag, cddl_size_control<Min * element_size, Max * element_size>("bstr"));
}

template <typename T, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_homogeneous_array_expr(CDDLContext &context, CDDLOptions options) {
    using value_type = std::remove_cvref_t<T>;
    using traits     = cddl_homogeneous_array_traits<value_type>;
    return text::format("#6.{}({})", traits::tag, cddl_type_expr<typename traits::array_type, PointerMode>(context, options));
}

template <typename T, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_multi_dimensional_array_expr(CDDLContext &context, CDDLOptions options) {
    using value_type = std::remove_cvref_t<T>;
    using traits     = cddl_multi_dimensional_array_traits<value_type>;
    auto dimensions  = parenthesize_choice(cddl_type_expr<typename traits::dimensions_type, PointerMode>(context, options));
    auto array       = parenthesize_choice(cddl_type_expr<typename traits::array_type, PointerMode>(context, options));
    return text::format("#6.{}([{}, {}])", traits::tag, dimensions, array);
}

template <std::size_t N> std::string join_cddl(const std::array<std::string, N> &items, std::string_view separator) {
    std::string result;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) {
            result += separator;
        }
        result += items[i];
    }
    return result;
}

inline std::string join_cddl(const std::vector<std::string> &items, std::string_view separator) {
    std::string result;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) {
            result += separator;
        }
        result += items[i];
    }
    return result;
}

template <typename T> consteval std::size_t cddl_std_enum_entry_count() {
#if CBOR_TAGS_HAS_STD_REFLECTION
    using value_type = std::remove_cvref_t<T>;
    return std::meta::enumerators_of(^^value_type).size();
#else
    return 0;
#endif
}

template <typename T> std::string cddl_enum_value_literal(T value);

template <typename T> struct cddl_enum_item {
    T                value;
    std::string_view name;
};

template <typename T> bool cddl_enum_value_less(T lhs, T rhs) {
    using underlying_type = std::underlying_type_t<std::remove_cvref_t<T>>;
    if constexpr (std::is_signed_v<underlying_type>) {
        return static_cast<std::intmax_t>(static_cast<underlying_type>(lhs)) <
               static_cast<std::intmax_t>(static_cast<underlying_type>(rhs));
    } else {
        return static_cast<std::uintmax_t>(static_cast<underlying_type>(lhs)) <
               static_cast<std::uintmax_t>(static_cast<underlying_type>(rhs));
    }
}

template <typename T> std::vector<std::string> cddl_enum_items_to_cddl(std::vector<cddl_enum_item<T>> items) {
    std::stable_sort(items.begin(), items.end(),
                     [](const auto &lhs, const auto &rhs) { return cddl_enum_value_less(lhs.value, rhs.value); });

    std::vector<std::string> cddl_items;
    cddl_items.reserve(items.size());
    for (const auto &[value, name] : items) {
        cddl_items.push_back(text::format("{}: {}", cddl_member_key(name), cddl_enum_value_literal(value)));
    }
    return cddl_items;
}

#if CBOR_TAGS_HAS_STD_REFLECTION
template <typename T, std::size_t I> consteval std::meta::info cddl_std_enum_entry() {
    using value_type = std::remove_cvref_t<T>;
    return std::meta::enumerators_of(^^value_type)[I];
}

template <typename T, std::size_t I> consteval std::string_view cddl_std_enum_entry_name() {
    return std::meta::identifier_of(cddl_std_enum_entry<T, I>());
}

template <typename T, std::size_t I> consteval std::remove_cvref_t<T> cddl_std_enum_entry_value() {
    return std::meta::extract<std::remove_cvref_t<T>>(cddl_std_enum_entry<T, I>());
}

template <typename T, std::size_t... Is>
std::vector<cddl_enum_item<std::remove_cvref_t<T>>> cddl_std_enum_items(std::index_sequence<Is...>) {
    using value_type = std::remove_cvref_t<T>;
    std::vector<cddl_enum_item<value_type>> items;
    items.reserve(sizeof...(Is));
    (items.push_back({cddl_std_enum_entry_value<T, Is>(), cddl_std_enum_entry_name<T, Is>()}), ...);
    return items;
}
#endif

template <typename T> constexpr std::size_t cddl_enum_entry_count() {
#if CBOR_TAGS_HAS_STD_REFLECTION
    return cddl_std_enum_entry_count<std::remove_cvref_t<T>>();
#elif CBOR_TAGS_HAS_MAGIC_ENUM_NAMES
    return magic_enum::enum_count<std::remove_cvref_t<T>>();
#else
    return 0;
#endif
}

template <typename T> std::string cddl_enum_value_literal(T value) {
    using underlying_type = std::underlying_type_t<std::remove_cvref_t<T>>;
    const auto underlying = static_cast<underlying_type>(value);
    if constexpr (std::is_signed_v<underlying_type>) {
        return text::format("{}", static_cast<std::intmax_t>(underlying));
    } else {
        return text::format("{}", static_cast<std::uintmax_t>(underlying));
    }
}

template <typename T> std::vector<std::string> cddl_enum_items() {
#if CBOR_TAGS_HAS_STD_REFLECTION
    return cddl_enum_items_to_cddl(
        cddl_std_enum_items<std::remove_cvref_t<T>>(std::make_index_sequence<cddl_enum_entry_count<std::remove_cvref_t<T>>()>{}));
#elif CBOR_TAGS_HAS_MAGIC_ENUM_NAMES
    using value_type = std::remove_cvref_t<T>;
    std::vector<cddl_enum_item<value_type>> items;
    if constexpr (cddl_enum_entry_count<value_type>() != 0) {
        constexpr auto entries = magic_enum::enum_entries<value_type>();
        items.reserve(entries.size());
        for (const auto &[value, name] : entries) {
            items.push_back({value, name});
        }
    }
    return cddl_enum_items_to_cddl(std::move(items));
#else
    return {};
#endif
}

template <typename T> bool cddl_use_named_enum(CDDLOptions options) {
    return options.enum_mode == CDDLEnumMode::named_values && cddl_enum_entry_count<T>() != 0;
}

template <typename T> std::string cddl_enum_underlying_expr() {
    using value_type = std::remove_cvref_t<T>;
    if constexpr (IsEnumUnsigned<value_type>) {
        return "uint";
    } else {
        return "int";
    }
}

template <typename T> std::string cddl_enum_expr(CDDLOptions options) {
    using value_type = std::remove_cvref_t<T>;
    auto items       = cddl_enum_items<value_type>();

    if (items.empty()) {
        return cddl_enum_underlying_expr<value_type>();
    }
    if (!options.row_options.format_by_rows) {
        return "&(" + join_cddl(items, ", ") + ")";
    }

    auto       result = std::string("&(\n");
    const auto indent = std::string(options.row_options.offset, ' ');
    result += indent;
    result += join_cddl(items, ",\n" + indent);
    result += "\n)";
    return result;
}

template <typename T>
std::string ensure_cddl_enum_definition(CDDLContext &context, CDDLOptions options, std::string_view preferred_name = {}) {
    using value_type = std::remove_cvref_t<T>;
    static_assert(IsEnum<value_type>, "CDDL enum definitions require an enum type");

    const auto key = cddl_enum_key<value_type>();
    if (auto *def = context.find_by_key(key)) {
        return std::string(def->name);
    }

    auto  name = unique_cddl_name(context, key, preferred_name.empty() ? cddl_type_name<value_type>() : preferred_name);
    auto &def  = context.reserve(key, name);
    auto  cddl = text::format("{} = {}", name, cddl_enum_expr<value_type>(options));
    def.cddl   = std::pmr::string(cddl, &context.memory_resource);
    def.state  = CDDLContext::DefinitionState::done;
    return std::string(def.name);
}

template <std::size_t N> std::string cddl_fixed_array_items_expr(const std::array<std::string, N> &items, CDDLOptions options) {
    if (!options.row_options.format_by_rows) {
        return "[" + join_cddl(items, ", ") + "]";
    }

    std::string result = "[\n";
    const auto  indent = std::string(options.row_options.offset, ' ');
    for (std::size_t i = 0; i < items.size(); ++i) {
        result += indent;
        result += items[i];
        if (i + 1 != items.size()) {
            result += ",\n";
        }
    }
    result += "\n]";
    return result;
}

template <cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable, typename... Ts>
std::string cddl_fixed_array_expr(CDDLContext &context, CDDLOptions options) {
    std::array<std::string, sizeof...(Ts)> items{cddl_type_expr<std::remove_cvref_t<Ts>, PointerMode>(context, options)...};
    return cddl_fixed_array_items_expr(items, options);
}

template <cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable, typename... Ts>
std::string cddl_payload_expr(CDDLContext &context, CDDLOptions options) {
    if constexpr (sizeof...(Ts) == 0) {
        return "[]";
    } else if constexpr (sizeof...(Ts) == 1) {
        return cddl_type_expr<std::remove_cvref_t<first_type_t<Ts...>>, PointerMode>(context, options);
    } else {
        return cddl_fixed_array_expr<PointerMode, Ts...>(context, options);
    }
}

template <typename Tuple, std::size_t Offset, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable, std::size_t... Is>
std::string cddl_payload_from_tuple(CDDLContext &context, CDDLOptions options, std::index_sequence<Is...>) {
    return cddl_payload_expr<PointerMode, std::tuple_element_t<Offset + Is, Tuple>...>(context, options);
}

template <typename Tuple, std::size_t Offset, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_payload_from_tuple(CDDLContext &context, CDDLOptions options) {
    using tuple_type               = std::remove_cvref_t<Tuple>;
    constexpr std::size_t size     = std::tuple_size_v<tuple_type>;
    constexpr std::size_t payloads = size >= Offset ? size - Offset : 0;
    return cddl_payload_from_tuple<tuple_type, Offset, PointerMode>(context, options, std::make_index_sequence<payloads>{});
}

#if CBOR_TAGS_HAS_NAMED_REFLECTION
template <typename T, std::size_t I, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_labeled_array_member_expr(CDDLContext &context, CDDLOptions options) {
    using value_type = std::remove_cvref_t<T>;
    using tuple_type = aggregate_tuple_t<value_type>;
    using field_type = std::remove_cvref_t<std::tuple_element_t<I, tuple_type>>;

    constexpr auto raw_name = detail::aggregate_member_name<value_type, I>();
    return text::format("{}: {}", cddl_member_key(raw_name), cddl_type_expr<field_type, PointerMode>(context, options));
}

template <typename T, std::size_t I, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string ensure_cddl_array_field_alias_definition(CDDLContext &context, CDDLOptions options) {
    using value_type = std::remove_cvref_t<T>;
    using tuple_type = aggregate_tuple_t<value_type>;
    using field_type = std::remove_cvref_t<std::tuple_element_t<I, tuple_type>>;

    constexpr auto raw_name = detail::aggregate_member_name<value_type, I>();
    const auto     name     = std::string(raw_name);
    const auto     key      = cddl_array_field_alias_key<value_type, I, PointerMode>();
    if (auto *def = context.find_by_key(key)) {
        return std::string(def->name);
    }
    if (!is_valid_cddl_id(name)) {
        throw std::invalid_argument(text::format("CDDL label_array_fields alias '{}' is not a valid CDDL rule name", name));
    }
    if (is_cddl_prelude_rule_name(name)) {
        throw std::invalid_argument(text::format("CDDL label_array_fields alias '{}' collides with a CDDL prelude rule", name));
    }
    if (context.contains_name(name)) {
        throw std::invalid_argument(text::format("CDDL label_array_fields alias '{}' collides with an existing definition", name));
    }

    auto &def  = context.reserve(key, name);
    auto  cddl = text::format("{} = {}", name, cddl_type_expr<field_type, PointerMode>(context, options));
    def.cddl   = std::pmr::string(cddl, &context.memory_resource);
    def.state  = CDDLContext::DefinitionState::done;
    return std::string(def.name);
}

template <typename T, std::size_t Offset, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable, std::size_t... Is>
std::string cddl_labeled_fixed_array_expr(CDDLContext &context, CDDLOptions options, std::index_sequence<Is...>) {
    std::array<std::string, sizeof...(Is)> items{cddl_labeled_array_member_expr<T, Offset + Is, PointerMode>(context, options)...};
    return cddl_fixed_array_items_expr(items, options);
}

template <typename T, std::size_t Offset, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_labeled_payload_from_aggregate(CDDLContext &context, CDDLOptions options) {
    using value_type               = std::remove_cvref_t<T>;
    using tuple_type               = aggregate_tuple_t<value_type>;
    constexpr std::size_t size     = std::tuple_size_v<tuple_type>;
    constexpr std::size_t payloads = size >= Offset ? size - Offset : 0;
    if constexpr (payloads == 0U) {
        return "[]";
    } else if constexpr (payloads == 1U) {
        return ensure_cddl_array_field_alias_definition<value_type, Offset, PointerMode>(context, options);
    } else {
        return cddl_labeled_fixed_array_expr<value_type, Offset, PointerMode>(context, options, std::make_index_sequence<payloads>{});
    }
}
#endif

template <typename T, std::size_t Offset, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_aggregate_payload_expr(CDDLContext &context, CDDLOptions options) {
    using value_type = std::remove_cvref_t<T>;
    using tuple_type = aggregate_tuple_t<value_type>;
    if (options.label_array_fields) {
#if CBOR_TAGS_HAS_NAMED_REFLECTION
        return cddl_labeled_payload_from_aggregate<value_type, Offset, PointerMode>(context, options);
#else
        throw std::invalid_argument("CDDLOptions::label_array_fields requires named reflection");
#endif
    }
    return cddl_payload_from_tuple<tuple_type, Offset, PointerMode>(context, options);
}

template <typename T> std::string cddl_tag_prefix() {
    using value_type = std::remove_cvref_t<T>;
    if constexpr (is_static_tag_t<value_type>::value) {
        return text::format("#6.{}", value_type::cbor_tag);
    } else {
        return "#6";
    }
}

template <typename T>
constexpr bool is_empty_cddl_aggregate_v = IsAggregate<std::remove_cvref_t<T>> && !IsTag<std::remove_cvref_t<T>> &&
                                           std::tuple_size_v<aggregate_tuple_t<std::remove_cvref_t<T>>> == 0;

template <typename T> constexpr bool is_cddl_tag_only_tuple_v = IsTagOnlyTuple<std::remove_cvref_t<T>>;

struct CDDLRootIdentity {
    std::string_view key;
    std::string_view name;
};

inline void reject_explicit_root_name_collision(CDDLContext &context, CDDLRootIdentity root) {
    const auto *existing = context.find_by_name(root.name);
    if (existing == nullptr) {
        return;
    }

    const std::string_view existing_key{existing->key.data(), existing->key.size()};
    if (existing_key != root.key) {
        throw std::invalid_argument(text::format("CDDL root_name '{}' collides with an existing definition", root.name));
    }
}

template <typename T, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_aggregate_expr(CDDLContext &context, CDDLOptions options) {
    using value_type = std::remove_cvref_t<T>;
    static_assert(!is_empty_cddl_aggregate_v<value_type>, "empty aggregate has no CBOR data item shape; CDDL schema unsupported");

    if constexpr (HasInlineTag<value_type>) {
        return text::format("#6.{}({})", value_type::cbor_tag, cddl_aggregate_payload_expr<value_type, 0, PointerMode>(context, options));
    } else if constexpr (HasStaticTag<value_type>) {
        using tag_type = std::remove_cvref_t<decltype(value_type::cbor_tag)>;
        return text::format("{}({})", cddl_tag_prefix<tag_type>(),
                            cddl_aggregate_payload_expr<value_type, 1, PointerMode>(context, options));
    } else if constexpr (HasDynamicTag<value_type>) {
        return text::format("#6({})", cddl_aggregate_payload_expr<value_type, 1, PointerMode>(context, options));
    } else {
        return cddl_aggregate_payload_expr<value_type, 0, PointerMode>(context, options);
    }
}

template <typename T, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_tuple_expr(CDDLContext &context, CDDLOptions options) {
    using value_type = std::remove_cvref_t<T>;
    using tuple_type = value_type;

    if constexpr (IsTaggedTuple<value_type>) {
        using tag_type = std::remove_cvref_t<std::tuple_element_t<0, tuple_type>>;
        return text::format("{}({})", cddl_tag_prefix<tag_type>(), cddl_payload_from_tuple<tuple_type, 1, PointerMode>(context, options));
    } else {
        return cddl_payload_from_tuple<tuple_type, 0, PointerMode>(context, options);
    }
}

template <typename T, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_sequence_expr(CDDLContext &context, CDDLOptions options) {
    using value_type = std::remove_cvref_t<T>;
    using item_type  = std::remove_cvref_t<typename value_type::value_type>;

    auto item = parenthesize_choice(cddl_type_expr<item_type, PointerMode>(context, options));
    if constexpr (is_std_array<value_type>::value) {
        return text::format("[{}*{} {}]", is_std_array<value_type>::size, is_std_array<value_type>::size, item);
    } else if constexpr (is_std_span<value_type>::value) {
        if constexpr (is_std_span<value_type>::extent != std::dynamic_extent) {
            return text::format("[{}*{} {}]", is_std_span<value_type>::extent, is_std_span<value_type>::extent, item);
        } else {
            return text::format("[* {}]", item);
        }
    } else {
        return text::format("[* {}]", item);
    }
}

template <typename T, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_map_expr(CDDLContext &context, CDDLOptions options) {
    using value_type  = std::remove_cvref_t<T>;
    using key_type    = std::remove_cvref_t<typename value_type::key_type>;
    using mapped_type = std::remove_cvref_t<typename value_type::mapped_type>;
    auto key          = parenthesize_choice(cddl_type_expr<key_type, PointerMode>(context, options));
    auto value        = parenthesize_choice(cddl_type_expr<mapped_type, PointerMode>(context, options));
    return text::format("{{* {} => {}}}", key, value);
}

template <std::size_t Min, std::size_t Max> std::string cddl_occurrence() { return text::format("{}*{}", Min, Max); }

template <std::size_t Min, std::size_t Max> std::string cddl_size_control(std::string_view base) {
    if constexpr (Min == Max) {
        return text::format("{} .size {}", base, Min);
    } else {
        return text::format("{} .size ({}..{})", base, Min, Max);
    }
}

template <std::size_t Min, std::size_t Max, typename T> consteval void validate_bounded_fixed_sequence() {
    using value_type = std::remove_cvref_t<T>;
    if constexpr (is_std_array<value_type>::value) {
        static_assert(Min <= is_std_array<value_type>::size && is_std_array<value_type>::size <= Max,
                      "bounded_size fixed array extent must be inside the configured CDDL size bounds");
    } else if constexpr (is_std_span<value_type>::value && is_std_span<value_type>::extent != std::dynamic_extent) {
        static_assert(Min <= is_std_span<value_type>::extent && is_std_span<value_type>::extent <= Max,
                      "bounded_size fixed span extent must be inside the configured CDDL size bounds");
    }
}

template <std::size_t Min, std::size_t Max, typename T> consteval void validate_bounded_fixed_string() {
    using value_type = std::remove_cvref_t<T>;
    if constexpr (is_std_array<value_type>::value) {
        static_assert(Min <= is_std_array<value_type>::size && is_std_array<value_type>::size <= Max,
                      "bounded_size fixed string extent must be inside the configured CDDL size bounds");
    } else if constexpr (is_std_span<value_type>::value && is_std_span<value_type>::extent != std::dynamic_extent) {
        static_assert(Min <= is_std_span<value_type>::extent && is_std_span<value_type>::extent <= Max,
                      "bounded_size fixed string extent must be inside the configured CDDL size bounds");
    }
}

template <std::size_t Min, std::size_t Max, typename T> std::string cddl_bounded_string_expr(std::string_view base) {
    using value_type = std::remove_cvref_t<T>;
    validate_bounded_fixed_string<Min, Max, value_type>();
    if constexpr (is_std_array<value_type>::value) {
        return cddl_size_control<is_std_array<value_type>::size, is_std_array<value_type>::size>(base);
    } else if constexpr (is_std_span<value_type>::value && is_std_span<value_type>::extent != std::dynamic_extent) {
        return cddl_size_control<is_std_span<value_type>::extent, is_std_span<value_type>::extent>(base);
    } else {
        return cddl_size_control<Min, Max>(base);
    }
}

template <typename T, std::size_t Min, std::size_t Max, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_bounded_sequence_expr(CDDLContext &context, CDDLOptions options) {
    using value_type = std::remove_cvref_t<T>;
    validate_bounded_fixed_sequence<Min, Max, value_type>();
    if constexpr (is_std_array<value_type>::value ||
                  (is_std_span<value_type>::value && is_std_span<value_type>::extent != std::dynamic_extent)) {
        return cddl_sequence_expr<value_type, PointerMode>(context, options);
    } else {
        using item_type = std::remove_cvref_t<typename value_type::value_type>;
        auto item       = parenthesize_choice(cddl_type_expr<item_type, PointerMode>(context, options));
        return text::format("[{} {}]", cddl_occurrence<Min, Max>(), item);
    }
}

template <typename T, std::size_t Min, std::size_t Max, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_bounded_map_expr(CDDLContext &context, CDDLOptions options) {
    using value_type  = std::remove_cvref_t<T>;
    using key_type    = std::remove_cvref_t<typename value_type::key_type>;
    using mapped_type = std::remove_cvref_t<typename value_type::mapped_type>;
    auto key          = parenthesize_choice(cddl_type_expr<key_type, PointerMode>(context, options));
    auto value        = parenthesize_choice(cddl_type_expr<mapped_type, PointerMode>(context, options));
    return text::format("{{{} {} => {}}}", cddl_occurrence<Min, Max>(), key, value);
}

template <typename T, std::size_t Min, std::size_t Max, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_bounded_array_range_expr(CDDLContext &context, CDDLOptions options) {
    using value_type = std::remove_cvref_t<T>;
    using item_type  = std::remove_cvref_t<typename value_type::value_type>;
    auto item        = parenthesize_choice(cddl_type_expr<item_type, PointerMode>(context, options));
    return text::format("[{} {}]", cddl_occurrence<Min, Max>(), item);
}

template <typename T, std::size_t Min, std::size_t Max, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_bounded_map_range_expr(CDDLContext &context, CDDLOptions options) {
    using value_type  = std::remove_cvref_t<T>;
    using key_type    = std::remove_cvref_t<typename value_type::key_type>;
    using mapped_type = std::remove_cvref_t<typename value_type::mapped_type>;
    auto key          = parenthesize_choice(cddl_type_expr<key_type, PointerMode>(context, options));
    auto value        = parenthesize_choice(cddl_type_expr<mapped_type, PointerMode>(context, options));
    return text::format("{{{} {} => {}}}", cddl_occurrence<Min, Max>(), key, value);
}

template <typename T, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_array_range_expr(CDDLContext &context, CDDLOptions options) {
    using value_type = std::remove_cvref_t<T>;
    using item_type  = std::remove_cvref_t<typename value_type::value_type>;
    auto item        = parenthesize_choice(cddl_type_expr<item_type, PointerMode>(context, options));
    return text::format("[* {}]", item);
}

template <typename T, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_map_range_expr(CDDLContext &context, CDDLOptions options) {
    using value_type  = std::remove_cvref_t<T>;
    using key_type    = std::remove_cvref_t<typename value_type::key_type>;
    using mapped_type = std::remove_cvref_t<typename value_type::mapped_type>;
    auto key          = parenthesize_choice(cddl_type_expr<key_type, PointerMode>(context, options));
    auto value        = parenthesize_choice(cddl_type_expr<mapped_type, PointerMode>(context, options));
    return text::format("{{* {} => {}}}", key, value);
}

template <typename T, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_bounded_size_expr(CDDLContext &context, CDDLOptions options) {
    using bounded_type = std::remove_cvref_t<T>;
    using wrapped_type = std::remove_cvref_t<typename bounded_type::value_type>;
    using render_type  = std::conditional_t<IsIndefiniteWrapper<wrapped_type>, indefinite_value_t<wrapped_type>, wrapped_type>;

    if constexpr (CDDLBoundedTaggedByteStringArray<render_type>) {
        return cddl_bounded_tagged_bstr_array_expr<render_type, bounded_type::min_size, bounded_type::max_size>();
    } else if constexpr (IsBinaryString<render_type> || IsBstrRangeWrapper<render_type>) {
        return cddl_bounded_string_expr<bounded_type::min_size, bounded_type::max_size, render_type>("bstr");
    } else if constexpr (IsTextString<render_type> || IsTstrRangeWrapper<render_type>) {
        return cddl_bounded_string_expr<bounded_type::min_size, bounded_type::max_size, render_type>("tstr");
    } else if constexpr (IsArrayRangeWrapper<render_type>) {
        return cddl_bounded_array_range_expr<render_type, bounded_type::min_size, bounded_type::max_size, PointerMode>(context, options);
    } else if constexpr (IsMapRangeWrapper<render_type>) {
        return cddl_bounded_map_range_expr<render_type, bounded_type::min_size, bounded_type::max_size, PointerMode>(context, options);
    } else if constexpr (IsMap<render_type>) {
        return cddl_bounded_map_expr<render_type, bounded_type::min_size, bounded_type::max_size, PointerMode>(context, options);
    } else if constexpr (IsArray<render_type>) {
        return cddl_bounded_sequence_expr<render_type, bounded_type::min_size, bounded_type::max_size, PointerMode>(context, options);
    } else {
        static_assert(always_false<render_type>::value, "bounded_size CDDL requires a string, array, map, or explicit range wrapper");
        return {};
    }
}

#if CBOR_TAGS_HAS_NAMED_REFLECTION
inline std::string cddl_row_indent(CDDLOptions options, std::size_t extra_indent = 0) {
    return std::string((options.row_options.current_indent + extra_indent) * options.row_options.offset, ' ');
}

inline CDDLOptions cddl_nested_row_options(CDDLOptions options) {
    ++options.row_options.current_indent;
    return options;
}

template <typename T, std::size_t I, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_named_member_entry(CDDLContext &context, CDDLOptions options) {
    using value_type = std::remove_cvref_t<T>;
    using tuple_type = aggregate_tuple_t<value_type>;
    using field_type = std::remove_cvref_t<std::tuple_element_t<I, tuple_type>>;

    constexpr auto raw_name = detail::aggregate_member_name<value_type, I>();
    if constexpr (IsNamedGroupWrapper<field_type>) {
        if (options.always_inline) {
            return cddl_named_group_expr<named_group_value_t<field_type>, PointerMode>(context, cddl_nested_row_options(options));
        }
        return ensure_cddl_named_group_definition<named_group_value_t<field_type>, PointerMode>(context, options, raw_name);
    } else if constexpr (IsNamedExtensionWrapper<field_type>) {
        using extension_type = named_extension_value_t<field_type>;
        static_assert(IsMap<extension_type> && IsTextString<typename extension_type::key_type>,
                      "as_named_extension requires a map with text-string keys");
        return text::format("* tstr => {}", cddl_type_expr<typename extension_type::mapped_type, PointerMode>(context, options));
    } else if constexpr (IsOptional<field_type>) {
        return text::format("? {}: {}", cddl_member_key(raw_name),
                            cddl_type_expr<typename field_type::value_type, PointerMode>(context, options));
    } else {
        return text::format("{}: {}", cddl_member_key(raw_name), cddl_type_expr<field_type, PointerMode>(context, options));
    }
}

template <typename T, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable, std::size_t... Is>
std::string cddl_named_entries(CDDLContext &context, CDDLOptions options, std::index_sequence<Is...>) {
    std::array<std::string, sizeof...(Is)> items{cddl_named_member_entry<T, Is, PointerMode>(context, options)...};
    return join_cddl(items, options.row_options.format_by_rows ? ",\n" + cddl_row_indent(options, 1) : ", ");
}

template <typename T, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_named_body(CDDLContext &context, CDDLOptions options, char open, char close) {
    using value_type            = std::remove_cvref_t<T>;
    constexpr auto member_count = detail::aggregate_member_count<value_type>();
    static_assert(detail::named_fixed_member_keys_are_unique<value_type>(),
                  "as_named_map/as_named_group fixed field names must be unique after flattening as_named_group members");
    static_assert(detail::named_flattened_extension_count<value_type>() <= 1U,
                  "as_named_map/as_named_group may contain at most one as_named_extension field after flattening as_named_group members");
    if (!options.row_options.format_by_rows) {
        return text::format("{}{}{}", open,
                            cddl_named_entries<value_type, PointerMode>(context, options, std::make_index_sequence<member_count>{}), close);
    }

    auto entries = cddl_named_entries<value_type, PointerMode>(context, options, std::make_index_sequence<member_count>{});
    if (!entries.empty()) {
        entries = cddl_row_indent(options, 1) + entries;
    }
    return text::format("{}\n{}\n{}{}", open, entries, cddl_row_indent(options), close);
}

template <typename T, cddl_shared_pointer_mode PointerMode> std::string cddl_named_map_expr(CDDLContext &context, CDDLOptions options) {
    return cddl_named_body<T, PointerMode>(context, options, '{', '}');
}

template <typename T, cddl_shared_pointer_mode PointerMode> std::string cddl_named_group_expr(CDDLContext &context, CDDLOptions options) {
    return cddl_named_body<T, PointerMode>(context, options, '(', ')');
}

template <typename T, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string ensure_cddl_named_definition(CDDLContext &context, CDDLOptions options, std::string_view preferred_name,
                                         std::string (*key_fn)(), std::string (*expr_fn)(CDDLContext &, CDDLOptions)) {
    const auto key = key_fn();
    if (auto *def = context.find_by_key(key)) {
        if (def->state == CDDLContext::DefinitionState::visiting) {
            def->recursive_reference = true;
        }
        return std::string(def->name);
    }

    auto  name = unique_cddl_name(context, key, preferred_name.empty() ? cddl_type_name<T>() : preferred_name);
    auto &def  = context.reserve(key, name);
    auto  body = expr_fn(context, options);
    auto  cddl = text::format("{} = {}", name, body);
    def.cddl   = std::pmr::string(cddl, &context.memory_resource);
    def.state  = CDDLContext::DefinitionState::done;
    return std::string(def.name);
}

template <typename T, cddl_shared_pointer_mode PointerMode>
std::string ensure_cddl_named_map_definition(CDDLContext &context, CDDLOptions options, std::string_view preferred_name) {
    using value_type = std::remove_cvref_t<T>;
    static_assert(IsAggregate<value_type>, "as_named_map requires an aggregate payload");
    return ensure_cddl_named_definition<value_type, PointerMode>(
        context, options, preferred_name, cddl_named_map_key<value_type, PointerMode>, cddl_named_map_expr<value_type, PointerMode>);
}

template <typename T, cddl_shared_pointer_mode PointerMode>
std::string ensure_cddl_named_group_definition(CDDLContext &context, CDDLOptions options, std::string_view preferred_name) {
    using value_type = std::remove_cvref_t<T>;
    static_assert(IsAggregate<value_type>, "as_named_group requires an aggregate payload");
    return ensure_cddl_named_definition<value_type, PointerMode>(
        context, options, preferred_name, cddl_named_group_key<value_type, PointerMode>, cddl_named_group_expr<value_type, PointerMode>);
}
#else
template <typename T, cddl_shared_pointer_mode PointerMode>
std::string ensure_cddl_named_map_definition(CDDLContext &, CDDLOptions, std::string_view) {
    static_assert(always_false<std::remove_cvref_t<T>>::value,
                  "as_named_map requires named reflection (C++26 std::meta or Boost.PFR field names)");
    return {};
}

template <typename T, cddl_shared_pointer_mode PointerMode>
std::string ensure_cddl_named_group_definition(CDDLContext &, CDDLOptions, std::string_view) {
    static_assert(always_false<std::remove_cvref_t<T>>::value,
                  "as_named_group requires named reflection (C++26 std::meta or Boost.PFR field names)");
    return {};
}
#endif

template <typename T, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
std::string cddl_rule_expr(CDDLContext &context, CDDLOptions options, std::string_view name) {
    return text::format("{} = {}", name, cddl_aggregate_expr<T, PointerMode>(context, options));
}

template <typename T, cddl_shared_pointer_mode PointerMode>
std::string ensure_cddl_definition(CDDLContext &context, CDDLOptions options, std::string_view preferred_name) {
    using value_type = std::remove_cvref_t<T>;
    if constexpr (IsAggregate<value_type> && !is_static_tag_t<value_type>::value && !is_dynamic_tag_t<value_type>) {
        const auto key = cddl_type_key<value_type, PointerMode>();
        if (auto *def = context.find_by_key(key)) {
            if (def->state == CDDLContext::DefinitionState::visiting) {
                def->recursive_reference = true;
            }
            return std::string(def->name);
        }

        auto  name = unique_cddl_name(context, key, preferred_name.empty() ? cddl_type_name<value_type>() : preferred_name);
        auto &def  = context.reserve(key, name);
        auto  body = cddl_aggregate_expr<value_type, PointerMode>(context, options);
        if (options.always_inline && preferred_name.empty() && !def.recursive_reference) {
            context.erase_by_key(key);
            return body;
        }

        auto cddl = text::format("{} = {}", name, body);
        def.cddl  = std::pmr::string(cddl, &context.memory_resource);
        def.state = CDDLContext::DefinitionState::done;
        return std::string(def.name);
    } else {
        return cddl_type_expr<value_type, PointerMode>(context, options);
    }
}

template <typename T, cddl_shared_pointer_mode PointerMode> std::string cddl_type_expr(CDDLContext &context, CDDLOptions options) {
    using value_type = std::remove_cvref_t<T>;
    if constexpr (CDDLScopedType<value_type>) {
        static_assert(always_false<value_type>::value, "CDDL scope wrappers are only valid as cddl_schema_to roots");
        return {};
    } else if constexpr (IsDynamicBoundedSizeWrapper<value_type>) {
        static_assert(always_false<value_type>::value,
                      "dynamic_bounded_size cannot be represented by type-based CDDL; use bounded_size<T, Min, Max>");
        return {};
    } else if constexpr (IsBoundedSizeWrapper<value_type>) {
        return cddl_bounded_size_expr<value_type, PointerMode>(context, options);
    } else if constexpr (IsBstrRangeWrapper<value_type> || IsBinaryString<value_type>) {
        return "bstr";
    } else if constexpr (IsTstrRangeWrapper<value_type> || IsTextString<value_type>) {
        return "tstr";
    } else if constexpr (IsArrayRangeWrapper<value_type>) {
        return cddl_array_range_expr<value_type, PointerMode>(context, options);
    } else if constexpr (IsMapRangeWrapper<value_type>) {
        return cddl_map_range_expr<value_type, PointerMode>(context, options);
    } else if constexpr (IsEnum<value_type>) {
        if constexpr (cddl_enum_entry_count<value_type>() != 0) {
            if (cddl_use_named_enum<value_type>(options)) {
                if (options.always_inline) {
                    return cddl_enum_expr<value_type>(options);
                }
                return ensure_cddl_enum_definition<value_type>(context, options);
            }
        }
        return cddl_enum_underlying_expr<value_type>();
    } else if constexpr (IsUnsigned<value_type>) {
        return "uint";
    } else if constexpr (IsNegative<value_type>) {
        return "nint";
    } else if constexpr (IsSigned<value_type>) {
        return "int";
    } else if constexpr (IsIndefiniteWrapper<value_type>) {
        return cddl_type_expr<indefinite_value_t<value_type>, PointerMode>(context, options);
    } else if constexpr (IsOptional<value_type>) {
        return text::format("{} / null", cddl_type_expr<typename value_type::value_type, PointerMode>(context, options));
    } else if constexpr (IsNullablePointer<value_type>) {
        using element_type = nullable_pointer_element_t<value_type>;
        static_assert(is_supported_nullable_pointer_v<value_type>,
                      "CDDL nullable pointer support requires std::unique_ptr<T> with the default deleter or std::shared_ptr<T>, and "
                      "T must be non-const, non-void, and non-array");
        static_assert(std::default_initializable<element_type>,
                      "CDDL nullable pointer support requires default-initializable pointee types because pointer decode constructs T");
        if constexpr (PointerMode == cddl_shared_pointer_mode::shared_graph && is_std_shared_ptr<value_type>::value) {
            return text::format("[0] / #6.28({}) / #6.29(uint)",
                                parenthesize_choice(cddl_type_expr<element_type, PointerMode>(context, options)));
        } else {
            return text::format("[0] / [1, {}]", parenthesize_choice(cddl_type_expr<element_type, PointerMode>(context, options)));
        }
    } else if constexpr (CDDLTaggedByteStringArray<value_type>) {
        return cddl_tagged_bstr_array_expr<value_type>();
    } else if constexpr (CDDLHomogeneousArray<value_type>) {
        return cddl_homogeneous_array_expr<value_type, PointerMode>(context, options);
    } else if constexpr (CDDLMultiDimensionalArray<value_type>) {
        return cddl_multi_dimensional_array_expr<value_type, PointerMode>(context, options);
    } else if constexpr (IsVariant<value_type>) {
        return detail::with_variant_alternatives<value_type>([&context, &options]<typename... Ts>() {
            constexpr auto matching_major_types = valid_concept_mapping_array_v<value_type>;
            static_assert(matching_major_types[MajorIndex::Tag] <= 1,
                          "CDDL for variant alternatives with duplicate or catch-all CBOR tag matches is unsupported");
            static_assert(!cddl_scoped_variant_has_tag_overlap<PointerMode, Ts...>(),
                          "CDDL for variant alternatives with duplicate or catch-all CBOR tag matches is unsupported");
            static_assert(matching_major_types[MajorIndex::DynamicTag] == 0,
                          "CDDL for variant alternatives with dynamic CBOR tags is unsupported");
            if constexpr (PointerMode == cddl_shared_pointer_mode::shared_graph) {
                constexpr auto direct_pointer_alternatives =
                    (std::size_t{0} + ... + (cddl_is_direct_nullable_pointer_alternative<Ts>() ? std::size_t{1} : std::size_t{0}));
                constexpr auto graph_vector_alternatives =
                    (std::size_t{0} + ... + (cddl_is_shared_graph_vector_alternative<Ts>() ? std::size_t{1} : std::size_t{0}));
                static_assert((!cddl_contains_unsupported_shared_graph_variant_pointer<Ts>() && ...),
                              "CDDL for variant alternatives containing indirect nullable smart pointers is unsupported");
                static_assert(direct_pointer_alternatives <= 1U,
                              "CDDL for variant alternatives containing multiple nullable smart pointers is unsupported");
                static_assert(direct_pointer_alternatives == 0U || matching_major_types[MajorIndex::Array] == 0U,
                              "CDDL for variant alternatives containing nullable smart pointers and array-shaped alternatives is "
                              "unsupported");
                static_assert(graph_vector_alternatives == 0U || matching_major_types[MajorIndex::Array] == 1U,
                              "CDDL for variant alternatives containing shared graph vector smart pointers and other array-shaped "
                              "alternatives is unsupported");
            } else {
                static_assert((!cddl_contains_nullable_pointer<Ts>() && ...),
                              "CDDL for variant alternatives containing nullable smart pointers is unsupported because runtime variant "
                              "decode is not extension-codec aware");
            }
            return join_cddl(std::array<std::string, sizeof...(Ts)>{cddl_type_expr<Ts, PointerMode>(context, options)...}, " / ");
        });
    } else if constexpr (IsArrayHeader<value_type>) {
        return "[* any]";
    } else if constexpr (IsMapHeader<value_type>) {
        return "{* any => any}";
    } else if constexpr (IsTagHeader<value_type>) {
        return "#6(any)";
    } else if constexpr (IsNamedMapWrapper<value_type>) {
        return ensure_cddl_named_map_definition<named_map_value_t<value_type>, PointerMode>(context, options);
    } else if constexpr (IsNamedGroupWrapper<value_type>) {
        return ensure_cddl_named_group_definition<named_group_value_t<value_type>, PointerMode>(context, options);
    } else if constexpr (IsNamedExtensionWrapper<value_type>) {
        static_assert(always_false<value_type>::value, "as_named_extension is only valid inside as_named_map aggregates");
        return {};
    } else if constexpr (IsMap<value_type>) {
        return cddl_map_expr<value_type, PointerMode>(context, options);
    } else if constexpr (IsArray<value_type>) {
        return cddl_sequence_expr<value_type, PointerMode>(context, options);
    } else if constexpr (is_static_tag_t<value_type>::value || is_dynamic_tag_t<value_type>) {
        return cddl_tag_prefix<value_type>();
    } else if constexpr (is_cddl_tag_only_tuple_v<value_type>) {
        static_assert(always_false<value_type>::value, "tag-only tuple has no CBOR payload; CDDL schema unsupported");
        return {};
    } else if constexpr (IsTuple<value_type>) {
        return cddl_tuple_expr<value_type, PointerMode>(context, options);
    } else if constexpr (IsAggregate<value_type>) {
        return ensure_cddl_definition<value_type, PointerMode>(context, options);
    } else if constexpr (IsSimple<value_type>) {
        if constexpr (IsBool<value_type>) {
            return "bool";
        } else if constexpr (IsFloat16<value_type>) {
            return "float16";
        } else if constexpr (IsFloat32<value_type>) {
            return "float32";
        } else if constexpr (IsFloat64<value_type>) {
            return "float64";
        } else if constexpr (IsNull<value_type>) {
            return "null";
        } else {
            return "#7.<0..23 / 32..255>";
        }
    } else {
        return cddl_type_name<value_type>();
    }
}

template <typename Context> decltype(auto) cddl_context_ref(Context &context) {
    if constexpr (IsReferenceWrapper<Context>) {
        return context.get();
    } else {
        return (context);
    }
}

inline void reject_unavailable_cddl_array_field_labels(CDDLOptions options) {
#if CBOR_TAGS_HAS_NAMED_REFLECTION
    (void)options;
#else
    if (options.label_array_fields) {
        throw std::invalid_argument("CDDLOptions::label_array_fields requires named reflection");
    }
#endif
}

template <typename T> std::string root_rule_name(CDDLOptions options) {
    using value_type = std::remove_cvref_t<T>;
    if (!options.root_name.empty()) {
        return sanitize_cddl_id(options.root_name);
    } else if constexpr (IsEnum<value_type>) {
        if (cddl_use_named_enum<value_type>(options)) {
            return cddl_type_name<value_type>();
        }
        return "root";
    } else if constexpr (IsAggregate<value_type> && !is_static_tag_t<value_type>::value && !is_dynamic_tag_t<value_type>) {
        return cddl_type_name<value_type>();
    } else {
        return "root";
    }
}

template <typename T> std::string tag_marker_root_expr(CDDLContext &context, CDDLOptions options) {
    (void)context;
    (void)options;
    return text::format("{}(any)", cddl_tag_prefix<std::remove_cvref_t<T>>());
}

template <typename T, typename OutputBuffer, cddl_shared_pointer_mode PointerMode = cddl_shared_pointer_mode::nullable>
void cddl_schema_root_expr_to(OutputBuffer &output_buffer, CDDLContext &cddl_context, CDDLOptions options) {
    using value_type       = std::remove_cvref_t<T>;
    auto root_name         = root_rule_name<value_type>(options);
    auto root_key          = text::format("__cddl_root:{}", root_name);
    bool reserved_root_key = false;
    if (!options.root_name.empty()) {
        reject_explicit_root_name_collision(cddl_context, {.key = root_key, .name = root_name});
    } else if (cddl_context.contains_name(root_name)) {
        root_name = unique_cddl_name(cddl_context, root_key, root_name);
        root_key  = text::format("__cddl_root:{}", root_name);
    }
    if (!cddl_context.contains_name(root_name)) {
        (void)cddl_context.reserve(root_key, root_name);
        reserved_root_key = true;
    }
    auto root_expr = cddl_type_expr<value_type, PointerMode>(cddl_context, options);
    if (reserved_root_key) {
        cddl_context.erase_by_key(root_key);
    }
    text::format_to(std::back_inserter(output_buffer), "{} = {}", root_name, root_expr);
}

template <typename T, cddl_shared_pointer_mode PointerMode, typename OutputBuffer, typename Context>
auto cddl_schema_to_impl(OutputBuffer &output_buffer, CDDLOptions options, Context &context) {
    using value_type   = std::remove_cvref_t<T>;
    auto &cddl_context = cddl_context_ref(context);
    debug::println("cddl_schema_to: {}", detail::short_type_name<T>());

    if constexpr (IsNamedMapWrapper<value_type>) {
        using named_value_type = named_map_value_t<value_type>;
        const auto requested_root_name =
            options.root_name.empty() ? cddl_type_name<named_value_type>() : sanitize_cddl_id(options.root_name);
        const auto root_key = cddl_named_map_key<named_value_type, PointerMode>();
        if (!options.root_name.empty()) {
            reject_explicit_root_name_collision(cddl_context, {.key = root_key, .name = requested_root_name});
        }
        (void)ensure_cddl_named_map_definition<named_value_type, PointerMode>(cddl_context, options, requested_root_name);
        if (const auto *root_def = cddl_context.find_by_key(root_key); root_def != nullptr) {
            text::format_to(std::back_inserter(output_buffer), "{}", root_def->cddl);
        }
    } else if constexpr (IsNamedGroupWrapper<value_type>) {
        using named_value_type = named_group_value_t<value_type>;
        const auto requested_root_name =
            options.root_name.empty() ? cddl_type_name<named_value_type>() : sanitize_cddl_id(options.root_name);
        const auto root_key = cddl_named_group_key<named_value_type, PointerMode>();
        if (!options.root_name.empty()) {
            reject_explicit_root_name_collision(cddl_context, {.key = root_key, .name = requested_root_name});
        }
        (void)ensure_cddl_named_group_definition<named_value_type, PointerMode>(cddl_context, options, requested_root_name);
        if (const auto *root_def = cddl_context.find_by_key(root_key); root_def != nullptr) {
            text::format_to(std::back_inserter(output_buffer), "{}", root_def->cddl);
        }
    } else if constexpr (IsEnum<value_type>) {
        if constexpr (cddl_enum_entry_count<value_type>() != 0) {
            if (cddl_use_named_enum<value_type>(options) && !options.always_inline) {
                const auto requested_root_name =
                    options.root_name.empty() ? cddl_type_name<value_type>() : sanitize_cddl_id(options.root_name);
                const auto root_key = cddl_enum_key<value_type>();
                if (!options.root_name.empty()) {
                    reject_explicit_root_name_collision(cddl_context, {.key = root_key, .name = requested_root_name});
                }
                (void)ensure_cddl_enum_definition<value_type>(cddl_context, options, requested_root_name);
                if (const auto *root_def = cddl_context.find_by_key(root_key); root_def != nullptr) {
                    text::format_to(std::back_inserter(output_buffer), "{}", root_def->cddl);
                }
            } else {
                cddl_schema_root_expr_to<value_type, OutputBuffer, PointerMode>(output_buffer, cddl_context, options);
            }
        } else {
            cddl_schema_root_expr_to<value_type, OutputBuffer, PointerMode>(output_buffer, cddl_context, options);
        }
    } else if constexpr (IsAggregate<value_type> && !is_static_tag_t<value_type>::value && !is_dynamic_tag_t<value_type>) {
        static_assert(!is_empty_cddl_aggregate_v<value_type>, "empty aggregate has no CBOR data item shape; CDDL schema unsupported");
        const auto requested_root_name = root_rule_name<value_type>(options);
        const auto root_key            = cddl_type_key<value_type, PointerMode>();
        if (!options.root_name.empty()) {
            reject_explicit_root_name_collision(cddl_context, {.key = root_key, .name = requested_root_name});
        }
        const auto  root_name = ensure_cddl_definition<value_type, PointerMode>(cddl_context, options, requested_root_name);
        const auto *root_def  = cddl_context.find_by_key(root_key);
        if (root_def != nullptr) {
            text::format_to(std::back_inserter(output_buffer), "{}", root_def->cddl);
        } else {
            text::format_to(std::back_inserter(output_buffer), "{} = {}", root_name,
                            cddl_aggregate_expr<value_type, PointerMode>(cddl_context, options));
        }
    } else if constexpr (is_static_tag_t<value_type>::value || is_dynamic_tag_t<value_type>) {
        text::format_to(std::back_inserter(output_buffer), "{} = {}", root_rule_name<value_type>(options),
                        tag_marker_root_expr<value_type>(cddl_context, options));
    } else {
        cddl_schema_root_expr_to<value_type, OutputBuffer, PointerMode>(output_buffer, cddl_context, options);
    }

    if constexpr (!IsReferenceWrapper<Context>) {
        std::string root_key;
        if constexpr (IsNamedMapWrapper<value_type>) {
            root_key = cddl_named_map_key<named_map_value_t<value_type>, PointerMode>();
        } else if constexpr (IsNamedGroupWrapper<value_type>) {
            root_key = cddl_named_group_key<named_group_value_t<value_type>, PointerMode>();
        } else if constexpr (IsEnum<value_type>) {
            if (cddl_use_named_enum<value_type>(options) && !options.always_inline) {
                root_key = cddl_enum_key<value_type>();
            }
        } else if constexpr (IsAggregate<value_type> && !is_static_tag_t<value_type>::value && !is_dynamic_tag_t<value_type>) {
            root_key = cddl_type_key<value_type, PointerMode>();
        }

        for (const auto &def : cddl_context.definitions | std::views::reverse) {
            const std::string_view key{def.key.data(), def.key.size()};
            if (key != root_key && def.state == CDDLContext::DefinitionState::done) {
                text::format_to(std::back_inserter(output_buffer), "\n{}", def.cddl);
            }
        }
    }
}

} // namespace detail

template <typename T, typename OutputBuffer, typename Context = detail::CDDLContext>
auto cddl_schema_to(OutputBuffer &output_buffer, CDDLOptions options, Context context) {
    detail::reject_unavailable_cddl_array_field_labels(options);

    using value_type = std::remove_cvref_t<T>;
    if constexpr (detail::CDDLScopedType<value_type>) {
        using traits = detail::cddl_scope_traits<value_type>;
        return detail::cddl_schema_to_impl<typename traits::value_type, traits::shared_pointer_mode>(output_buffer, options, context);
    } else {
        return detail::cddl_schema_to_impl<value_type, detail::cddl_shared_pointer_mode::nullable>(output_buffer, options, context);
    }
}

template <typename CborBuffer, typename OutputBuffer>
auto buffer_annotate(const CborBuffer &cbor_buffer, OutputBuffer &output_buffer, AnnotationOptions options = {}) {
    if (options.diagnostic_data) {
        throw std::runtime_error("Diagnostic data not supported");
    }
    if (cbor_buffer.empty()) {
        return;
    }
    if (options.mode == AnnotationMode::smart) {
        detail::buffer_annotate_smart(cbor_buffer, output_buffer, options);
        return;
    }

    auto dec = make_decoder(cbor_buffer);

    detail::catch_all_variant value;
    std::stack<size_t>        indent_stack;

    auto indentation_visitor = [&indent_stack](auto &&value) {
        if constexpr (IsArrayHeader<std::remove_cvref_t<decltype(value)>>) {
            indent_stack.push(value.size + 1);
            return true;
        } else if constexpr (IsMapHeader<std::remove_cvref_t<decltype(value)>>) {
            indent_stack.push(value.size * 2 + 1);
            return true;
        } else if constexpr (IsTagHeader<std::remove_cvref_t<decltype(value)>>) {
            indent_stack.push(1 + 1);
            return true;
        } else {
            return false;
        }
    };
    constexpr auto string_size_visitor = [](auto &&value) {
        using value_type = std::remove_cvref_t<decltype(value)>;
        if constexpr (IsTextHeader<value_type> || IsBinaryHeader<value_type>) {
            return value.size;
        } else {
            return std::uint64_t{0};
        }
    };

    constexpr auto string_length_to_header_size = [](std::uint64_t length) {
        if (length < 24) {
            return 1;
        } else if (length <= std::numeric_limits<std::uint8_t>::max()) {
            return 2;
        } else if (length <= std::numeric_limits<std::uint16_t>::max()) {
            return 3;
        } else if (length <= std::numeric_limits<std::uint32_t>::max()) {
            return 5;
        } else {
            return 9;
        }
    };

    auto it = dec.tell();

    while (dec(value)) {
        auto next_it       = dec.tell();
        auto should_indent = std::visit(indentation_visitor, value);

        if (std::holds_alternative<as_text_any>(value) || std::holds_alternative<as_bstr_any>(value)) {
            auto size        = std::visit(string_size_visitor, value);
            auto header_size = string_length_to_header_size(size);
            detail::format_bytes(output_buffer, it, it + 1, options);                                          // Major type
            detail::format_bytes(output_buffer, it + 1, it + header_size, {.current_indent = 0, .offset = 1}); // extra header
            text::format_to(std::back_inserter(output_buffer), "\n");
            options.current_indent++;
            options.offset++;
            detail::format_bytes(output_buffer, it + header_size, next_it, options);
            options.current_indent--;
            options.offset--;
        } else {
            detail::format_bytes(output_buffer, it, it + 1, options);
            detail::format_bytes(output_buffer, it + 1, next_it, {.current_indent = 0, .offset = 1});
        }

        if (!indent_stack.empty()) {
            indent_stack.top()--;

            if (indent_stack.top() == 0) {
                indent_stack.pop();
                options.current_indent--;
            }
        }
        options.current_indent += should_indent;
        options.offset = indent_stack.size();
        text::format_to(std::back_inserter(output_buffer), "\n");
        it = next_it;
    }
}

template <typename OutputBuffer> constexpr void cddl_prelude_to(OutputBuffer &buffer) {
    text::format_to(std::back_inserter(buffer), "any = #\n"
                                                "\n"
                                                "uint = #0\n"
                                                "nint = #1\n"
                                                "int = uint / nint\n"
                                                "\n"
                                                "bstr = #2\n"
                                                "bytes = bstr\n"
                                                "tstr = #3\n"
                                                "text = tstr\n"
                                                "\n"
                                                "tdate = #6.0(tstr)\n"
                                                "time = #6.1(number)\n"
                                                "number = int / float\n"
                                                "biguint = #6.2(bstr)\n"
                                                "bignint = #6.3(bstr)\n"
                                                "bigint = biguint / bignint\n"
                                                "integer = int / bigint\n"
                                                "unsigned = uint / biguint\n"
                                                "decfrac = #6.4([e10: int, m: integer])\n"
                                                "bigfloat = #6.5([e2: int, m: integer])\n"
                                                "eb64url = #6.21(any)\n"
                                                "eb64legacy = #6.22(any)\n"
                                                "eb16 = #6.23(any)\n"
                                                "encoded-cbor = #6.24(bstr)\n"
                                                "uri = #6.32(tstr)\n"
                                                "b64url = #6.33(tstr)\n"
                                                "b64legacy = #6.34(tstr)\n"
                                                "regexp = #6.35(tstr)\n"
                                                "mime-message = #6.36(tstr)\n"
                                                "cbor-any = #6.55799(any)\n"
                                                "\n"
                                                "float16 = #7.25\n"
                                                "float32 = #7.26\n"
                                                "float64 = #7.27\n"
                                                "float16-32 = float16 / float32\n"
                                                "float32-64 = float32 / float64\n"
                                                "float = float16-32 / float64\n"
                                                "\n"
                                                "false = #7.20\n"
                                                "true = #7.21\n"
                                                "bool = false / true\n"
                                                "nil = #7.22\n"
                                                "null = nil\n"
                                                "undefined = #7.23\n");
}

template <typename OutputBuffer> void append_diagnostic_separator(OutputBuffer &output_buffer, bool format_by_rows) {
    text::format_to(std::back_inserter(output_buffer), "{}", format_by_rows ? ",\n" : ", ");
}

template <typename OutputBuffer, typename Iterator>
void append_escaped_diagnostic_text(OutputBuffer &output_buffer, Iterator begin, Iterator end) {
    text::format_to(std::back_inserter(output_buffer), "\"");
    for (; begin != end; ++begin) {
        const auto value = static_cast<unsigned char>(*begin);
        switch (value) {
        case '"': text::format_to(std::back_inserter(output_buffer), "\\\""); break;
        case '\\': text::format_to(std::back_inserter(output_buffer), "\\\\"); break;
        case '\n': text::format_to(std::back_inserter(output_buffer), "\\n"); break;
        case '\r': text::format_to(std::back_inserter(output_buffer), "\\r"); break;
        case '\t': text::format_to(std::back_inserter(output_buffer), "\\t"); break;
        default:
            if (value < 0x20) {
                text::format_to(std::back_inserter(output_buffer), "\\x{:02x}", value);
            } else {
                text::format_to(std::back_inserter(output_buffer), "{}", static_cast<char>(value));
            }
            break;
        }
    }
    text::format_to(std::back_inserter(output_buffer), "\"");
}

namespace detail {

struct utf8_prefix {
    std::uint8_t first{};
    std::uint8_t second{};
};

template <typename T> [[nodiscard]] constexpr std::uint8_t utf8_byte(T value) noexcept {
    if constexpr (std::same_as<std::remove_cvref_t<T>, std::byte>) {
        return std::to_integer<std::uint8_t>(value);
    } else {
        return static_cast<std::uint8_t>(static_cast<unsigned char>(value));
    }
}

[[nodiscard]] constexpr bool is_utf8_continuation_byte(std::uint8_t value) noexcept { return (value & 0xC0U) == 0x80U; }

[[nodiscard]] constexpr std::size_t utf8_sequence_length(std::uint8_t value) noexcept {
    if (value <= 0x7FU) {
        return 1U;
    }
    if (value >= 0xC2U && value <= 0xDFU) {
        return 2U;
    }
    if (value >= 0xE0U && value <= 0xEFU) {
        return 3U;
    }
    if (value >= 0xF0U && value <= 0xF4U) {
        return 4U;
    }
    return 0U;
}

[[nodiscard]] constexpr bool is_valid_utf8_second_byte(utf8_prefix prefix) noexcept {
    return !((prefix.first == 0xE0U && prefix.second < 0xA0U) || (prefix.first == 0xEDU && prefix.second > 0x9FU) ||
             (prefix.first == 0xF0U && prefix.second < 0x90U) || (prefix.first == 0xF4U && prefix.second > 0x8FU));
}

template <typename Iterator, typename Sentinel> [[nodiscard]] bool is_valid_utf8(Iterator begin, Sentinel end) noexcept {
    while (begin != end) {
        std::array<std::uint8_t, 4> sequence{};
        sequence[0] = utf8_byte(*begin);
        ++begin;

        const auto sequence_length = utf8_sequence_length(sequence[0]);
        if (sequence_length == 0U) {
            return false;
        }

        for (std::size_t offset = 1U; offset < sequence_length; ++offset) {
            if (begin == end) {
                return false;
            }
            sequence[offset] = utf8_byte(*begin);
            ++begin;
            if (!is_utf8_continuation_byte(sequence[offset])) {
                return false;
            }
        }

        if (sequence_length > 1U && !is_valid_utf8_second_byte({.first = sequence[0], .second = sequence[1]})) {
            return false;
        }
    }
    return true;
}

template <std::ranges::input_range Range> [[nodiscard]] bool is_valid_utf8(Range &&range) noexcept {
    return is_valid_utf8(std::ranges::begin(range), std::ranges::end(range));
}

template <typename OutputBuffer> struct smart_annotator {
    const std::vector<std::byte> &bytes;
    OutputBuffer                 &output_buffer;
    AnnotationOptions             options;
    std::size_t                   position{};

    struct item_header {
        std::size_t  begin{};
        std::uint8_t major{};
        std::uint8_t additional_info{};
    };

    [[nodiscard]] bool empty() const noexcept { return position >= bytes.size(); }

    [[nodiscard]] std::uint8_t byte_at(std::size_t index) const { return std::to_integer<std::uint8_t>(bytes[index]); }

    [[nodiscard]] bool next_is_break() const noexcept { return !empty() && bytes[position] == static_cast<std::byte>(0xFF); }

    [[nodiscard]] static std::size_t checked_add(std::size_t lhs, std::size_t rhs) {
        if (lhs > std::numeric_limits<std::size_t>::max() - rhs) {
            throw std::runtime_error("CBOR annotation size overflow");
        }
        return lhs + rhs;
    }

    [[nodiscard]] static std::size_t checked_mul(std::size_t lhs, std::size_t rhs) {
        if (lhs != 0U && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
            throw std::runtime_error("CBOR annotation size overflow");
        }
        return lhs * rhs;
    }

    void check_depth(std::size_t depth) const {
        if (depth >= options.max_structure_depth) {
            throw std::runtime_error("CBOR annotation nesting depth exceeded");
        }
    }

    std::uint8_t read_byte() {
        if (empty()) {
            throw std::runtime_error("Unexpected end of CBOR annotation input");
        }
        return byte_at(position++);
    }

    std::uint16_t read_uint16_raw() {
        const auto byte0 = static_cast<std::uint16_t>(read_byte());
        const auto byte1 = static_cast<std::uint16_t>(read_byte());
        return static_cast<std::uint16_t>((byte0 << 8U) | byte1);
    }

    std::uint32_t read_uint32_raw() {
        const auto byte0 = static_cast<std::uint32_t>(read_byte());
        const auto byte1 = static_cast<std::uint32_t>(read_byte());
        const auto byte2 = static_cast<std::uint32_t>(read_byte());
        const auto byte3 = static_cast<std::uint32_t>(read_byte());
        return (byte0 << 24U) | (byte1 << 16U) | (byte2 << 8U) | byte3;
    }

    std::uint64_t read_uint64_raw() {
        const auto byte0 = static_cast<std::uint64_t>(read_byte());
        const auto byte1 = static_cast<std::uint64_t>(read_byte());
        const auto byte2 = static_cast<std::uint64_t>(read_byte());
        const auto byte3 = static_cast<std::uint64_t>(read_byte());
        const auto byte4 = static_cast<std::uint64_t>(read_byte());
        const auto byte5 = static_cast<std::uint64_t>(read_byte());
        const auto byte6 = static_cast<std::uint64_t>(read_byte());
        const auto byte7 = static_cast<std::uint64_t>(read_byte());
        return (byte0 << 56U) | (byte1 << 48U) | (byte2 << 40U) | (byte3 << 32U) | (byte4 << 24U) | (byte5 << 16U) | (byte6 << 8U) | byte7;
    }

    std::uint64_t read_argument(std::uint8_t additional_info) {
        std::uint64_t value{};
        auto          status = status_code::success;
        const auto    ok     = read_cbor_argument(additional_info, value, status, [this](std::uint8_t &byte_value) {
            byte_value = read_byte();
            return true;
        });
        if (!ok) {
            throw std::runtime_error("Invalid CBOR additional information");
        }
        return value;
    }

    [[nodiscard]] std::string hex_range(std::size_t begin, std::size_t end, bool spaces) const {
        std::string result;
        for (auto index = begin; index < end; ++index) {
            if (spaces && index != begin) {
                result.push_back(' ');
            }
            text::format_to(std::back_inserter(result), "{:02x}", byte_at(index));
        }
        return result;
    }

    [[nodiscard]] std::string header_hex(std::size_t begin, std::size_t end) const {
        if (end - begin <= 1U) {
            return hex_range(begin, end, false);
        }
        auto result = hex_range(begin, begin + 1U, false);
        result.push_back(' ');
        result.append(hex_range(begin + 1U, end, false));
        return result;
    }

    [[nodiscard]] std::string indent(std::size_t depth) const {
        const auto  levels = checked_add(options.current_indent, depth);
        std::string result(checked_add(checked_mul(levels, options.indent_width), options.offset), ' ');
        return result;
    }

    void ensure_output_capacity(std::size_t additional) const {
        if (additional > options.max_output_size || output_buffer.size() > options.max_output_size - additional) {
            throw std::runtime_error("CBOR annotation output size limit exceeded");
        }
    }

    void emit_annotated_line(std::size_t depth, std::string left, std::string_view comment) {
        left.insert(0, indent(depth));
        if (left.size() >= options.annotation_column) {
            throw std::runtime_error("CBOR annotation column too narrow");
        }
        const auto padding        = options.annotation_column - left.size();
        const auto comment_indent = checked_mul(depth, options.comment_indent_width);
        const auto output_size =
            checked_add(checked_add(checked_add(checked_add(left.size(), padding), 2U), comment_indent), checked_add(comment.size(), 1U));
        ensure_output_capacity(output_size);
        text::format_to(std::back_inserter(output_buffer), "{}{}# {}{}\n", left, std::string(padding, ' '),
                        std::string(comment_indent, ' '), comment);
    }

    void emit_plain_line(std::size_t depth, std::string left) {
        left.insert(0, indent(depth));
        if (left.size() >= options.annotation_column) {
            throw std::runtime_error("CBOR annotation column too narrow");
        }
        ensure_output_capacity(left.size() + 1U);
        text::format_to(std::back_inserter(output_buffer), "{}\n", left);
    }

    void emit_header(std::size_t depth, std::size_t begin, std::size_t end, std::string_view comment) {
        emit_annotated_line(depth, header_hex(begin, end), comment);
    }

    [[nodiscard]] std::size_t payload_bytes_per_line(std::size_t depth) const {
        const auto left_indent = indent(depth).size();
        if (left_indent + 2U >= options.annotation_column) {
            throw std::runtime_error("CBOR annotation column too narrow for payload");
        }
        auto max_bytes = (options.annotation_column - left_indent - 1U) / 2U;
        if (options.max_depth != std::numeric_limits<std::size_t>::max()) {
            max_bytes = std::min(max_bytes, options.max_depth);
        }
        if (max_bytes == 0U) {
            throw std::runtime_error("CBOR annotation payload wrap width too small");
        }
        return max_bytes;
    }

    void emit_payload(std::size_t depth, std::size_t begin, std::size_t length, std::string_view comment) {
        if (length == 0U) {
            return;
        }
        check_depth(depth);
        const auto bytes_per_line = payload_bytes_per_line(depth);
        auto       offset         = std::size_t{};
        while (offset < length) {
            const auto chunk = std::min(bytes_per_line, length - offset);
            auto       left  = hex_range(begin + offset, begin + offset + chunk, false);
            if (offset == 0U) {
                emit_annotated_line(depth, std::move(left), comment);
            } else {
                emit_plain_line(depth, std::move(left));
            }
            offset += chunk;
        }
    }

    [[nodiscard]] std::string text_comment(std::size_t begin, std::size_t length) const {
        std::string text;
        text.reserve(length);
        for (auto index = begin; index < begin + length; ++index) {
            text.push_back(static_cast<char>(byte_at(index)));
        }

        std::string result;
        cbor::tags::append_escaped_diagnostic_text(result, text.begin(), text.end());
        return result;
    }

    [[nodiscard]] std::string bytes_comment(std::size_t begin, std::size_t length) const {
        return text::format("h'{}'", hex_range(begin, begin + length, false));
    }

    [[nodiscard]] std::size_t text_comment_size(std::size_t begin, std::size_t length) const {
        auto size = std::size_t{2};
        for (auto index = begin; index < begin + length; ++index) {
            const auto value = byte_at(index);
            if (value == static_cast<std::uint8_t>('"') || value == static_cast<std::uint8_t>('\\') ||
                value == static_cast<std::uint8_t>('\n') || value == static_cast<std::uint8_t>('\r') ||
                value == static_cast<std::uint8_t>('\t')) {
                size = checked_add(size, 2U);
            } else if (value < 0x20U) {
                size = checked_add(size, 4U);
            } else {
                size = checked_add(size, 1U);
            }
        }
        return size;
    }

    [[nodiscard]] static std::size_t bytes_comment_size(std::size_t length) { return checked_add(3U, checked_mul(length, 2U)); }

    [[nodiscard]] static std::string negative_comment(std::uint64_t argument) {
        return text::format("negative({})", detail::negative_diagnostic_text(negative{argument + 1U}));
    }

    [[nodiscard]] static std::string tag_comment(std::uint64_t tag) {
        switch (tag) {
        case 0: return "tdate, tag(0)";
        case 1: return "time, tag(1)";
        case 21: return "eb64url, tag(21)";
        case 22: return "eb64legacy, tag(22)";
        case 23: return "eb16, tag(23)";
        case 24: return "encoded-cbor, tag(24)";
        case 32: return "uri, tag(32)";
        case 33: return "b64url, tag(33)";
        case 34: return "b64legacy, tag(34)";
        case 35: return "regexp, tag(35)";
        case 36: return "mime-message, tag(36)";
        case 55799: return "cbor-any, tag(55799)";
        default: return text::format("tag({})", tag);
        }
    }

    [[nodiscard]] static std::string simple_comment(std::uint8_t value) {
        switch (value) {
        case 20: return "false, simple(20)";
        case 21: return "true, simple(21)";
        case 22: return "null, simple(22)";
        case 23: return "undefined, simple(23)";
        default: return text::format("simple({})", value);
        }
    }

    [[nodiscard]] std::string text_or_non_utf8_comment(std::size_t begin, std::size_t length) const {
        const auto payload_begin = bytes.begin() + static_cast<std::ptrdiff_t>(begin);
        const auto payload_end   = bytes.begin() + static_cast<std::ptrdiff_t>(begin + length);
        if (!is_valid_utf8(payload_begin, payload_end)) {
            auto comment = text::format("non-utf8({})", length);
            ensure_output_capacity(comment.size());
            return comment;
        }
        ensure_output_capacity(text_comment_size(begin, length));
        return text_comment(begin, length);
    }

    void ensure_payload_available(std::uint64_t length) const {
        const auto remaining = bytes.size() - position;
        if (length > remaining) {
            throw std::runtime_error("Unexpected end of CBOR annotation payload");
        }
    }

    void parse_string(std::size_t depth, item_header header) {
        check_depth(depth);
        const auto kind =
            header.major == static_cast<std::uint8_t>(major_type::TextString) ? std::string_view{"text"} : std::string_view{"bytes"};
        if (header.additional_info == 31U) {
            emit_header(depth, header.begin, position, text::format("{}(*)", kind));
            while (true) {
                if (empty()) {
                    throw std::runtime_error("Unterminated indefinite CBOR string");
                }
                if (next_is_break()) {
                    consume_break(depth + 1U);
                    return;
                }
                const auto chunk_begin  = position;
                const auto initial_byte = read_byte();
                const auto chunk_major  = initial_byte >> 5U;
                const auto chunk_info   = initial_byte & 0x1FU;
                if (chunk_major != header.major || chunk_info == 31U) {
                    throw std::runtime_error("Invalid indefinite CBOR string chunk");
                }
                parse_string(depth + 1U, {.begin           = chunk_begin,
                                          .major           = static_cast<std::uint8_t>(chunk_major),
                                          .additional_info = static_cast<std::uint8_t>(chunk_info)});
            }
        }

        const auto length = read_argument(header.additional_info);
        emit_header(depth, header.begin, position, text::format("{}({})", kind, length));
        ensure_payload_available(length);
        const auto payload_begin = position;
        const auto payload_size  = static_cast<std::size_t>(length);
        position += payload_size;
        const auto is_text = header.major == static_cast<std::uint8_t>(major_type::TextString);
        if (!is_text) {
            ensure_output_capacity(bytes_comment_size(payload_size));
        }
        const auto comment = is_text ? text_or_non_utf8_comment(payload_begin, payload_size) : bytes_comment(payload_begin, payload_size);
        emit_payload(depth + 1U, payload_begin, payload_size, comment);
    }

    void parse_array(std::size_t depth, item_header header) {
        if (header.additional_info == 31U) {
            emit_header(depth, header.begin, position, "array(*)");
            while (true) {
                if (empty()) {
                    throw std::runtime_error("Unterminated indefinite CBOR array");
                }
                if (next_is_break()) {
                    consume_break(depth + 1U);
                    return;
                }
                parse_item(depth + 1U);
            }
        }

        const auto size = read_argument(header.additional_info);
        emit_header(depth, header.begin, position, text::format("array({})", size));
        for (std::uint64_t index = 0; index < size; ++index) {
            parse_item(depth + 1U);
        }
    }

    void parse_map(std::size_t depth, item_header header) {
        if (header.additional_info == 31U) {
            emit_header(depth, header.begin, position, "map(*)");
            while (true) {
                if (empty()) {
                    throw std::runtime_error("Unterminated indefinite CBOR map");
                }
                if (next_is_break()) {
                    consume_break(depth + 1U);
                    return;
                }
                parse_item(depth + 1U);
                if (empty() || next_is_break()) {
                    throw std::runtime_error("Indefinite CBOR map missing value");
                }
                parse_item(depth + 1U);
            }
        }

        const auto size = read_argument(header.additional_info);
        emit_header(depth, header.begin, position, text::format("map({})", size));
        for (std::uint64_t index = 0; index < size; ++index) {
            parse_item(depth + 1U);
            parse_item(depth + 1U);
        }
    }

    void parse_tag(std::size_t depth, item_header header) {
        const auto tag = read_argument(header.additional_info);
        emit_header(depth, header.begin, position, tag_comment(tag));
        parse_item(depth + 1U);
    }

    void parse_simple(std::size_t depth, item_header header) {
        if (header.additional_info == 31U) {
            throw std::runtime_error("CBOR break outside indefinite item");
        }
        if (header.additional_info < 24U) {
            emit_header(depth, header.begin, position, simple_comment(header.additional_info));
            return;
        }

        switch (header.additional_info) {
        case 24: {
            const auto value = read_byte();
            if (value >= 24U && value <= 31U) {
                throw std::runtime_error("Invalid CBOR simple value");
            }
            emit_header(depth, header.begin, position, simple_comment(value));
            return;
        }
        case 25: {
            const auto bits  = read_uint16_raw();
            const auto value = static_cast<double>(float16_t{bits});
            emit_header(depth, header.begin, position, text::format("float16({})", value));
            return;
        }
        case 26: {
            const auto bits  = read_uint32_raw();
            const auto value = std::bit_cast<float>(bits);
            emit_header(depth, header.begin, position, text::format("float32({})", value));
            return;
        }
        case 27: {
            const auto bits  = read_uint64_raw();
            const auto value = std::bit_cast<double>(bits);
            emit_header(depth, header.begin, position, text::format("float64({})", value));
            return;
        }
        default: throw std::runtime_error("Invalid CBOR simple additional information");
        }
    }

    void consume_break(std::size_t depth) {
        check_depth(depth);
        const auto header_begin = position;
        const auto initial_byte = read_byte();
        if (initial_byte != 0xFFU) {
            throw std::runtime_error("Expected CBOR break");
        }
        emit_header(depth, header_begin, position, "break");
    }

    void parse_item(std::size_t depth) {
        check_depth(depth);
        const auto header_begin = position;
        const auto initial_byte = read_byte();
        const auto header       = item_header{.begin           = header_begin,
                                              .major           = static_cast<std::uint8_t>(initial_byte >> 5U),
                                              .additional_info = static_cast<std::uint8_t>(initial_byte & 0x1FU)};

        switch (header.major) {
        case 0: {
            const auto value = read_argument(header.additional_info);
            emit_header(depth, header.begin, position, text::format("unsigned({})", value));
            return;
        }
        case 1: {
            const auto value = read_argument(header.additional_info);
            emit_header(depth, header.begin, position, negative_comment(value));
            return;
        }
        case 2:
        case 3: parse_string(depth, header); return;
        case 4: parse_array(depth, header); return;
        case 5: parse_map(depth, header); return;
        case 6: parse_tag(depth, header); return;
        case 7: parse_simple(depth, header); return;
        default: throw std::runtime_error("Invalid CBOR major type");
        }
    }

    void annotate_sequence() {
        while (!empty()) {
            parse_item(0);
        }
    }
};

template <typename CborBuffer, typename OutputBuffer>
void buffer_annotate_smart(const CborBuffer &cbor_buffer, OutputBuffer &output_buffer, AnnotationOptions options) {
    if (cbor_buffer.size() > options.max_input_size) {
        throw std::runtime_error("CBOR annotation input size limit exceeded");
    }

    std::vector<std::byte> bytes;
    bytes.reserve(cbor_buffer.size());
    for (const auto value : cbor_buffer) {
        if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, std::byte>) {
            bytes.push_back(value);
        } else {
            bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
        }
    }

    std::string staged_output;
    smart_annotator<std::string>{.bytes = bytes, .output_buffer = staged_output, .options = options}.annotate_sequence();
    text::format_to(std::back_inserter(output_buffer), "{}", staged_output);
}

} // namespace detail

template <typename OutputBuffer, typename Decoder> struct diagnostic_visitor;

template <typename OutputBuffer, typename Decoder> struct diagnostic_visitor {
    OutputBuffer     &output_buffer;
    Decoder          &dec;
    DiagnosticOptions options;

    constexpr void check_depth() const {
        if (options.current_depth >= options.max_depth) {
            throw std::runtime_error("CBOR diagnostic nesting depth exceeded");
        }
    }

    constexpr DiagnosticOptions child_options() const {
        auto child = options;
        ++child.current_depth;
        return child;
    }

    template <IsMapHeader T> constexpr void operator()(const T &arg) {
        check_depth();
        const auto format_by_rows = options.row_options.format_by_rows;
        auto       base_offset    = std::string(options.row_options.offset * options.row_options.current_indent * format_by_rows, ' ');
        text::format_to(std::back_inserter(output_buffer), "{{{}", options.row_options.format_by_rows ? "\n" : "");
        options.row_options.current_indent++;
        auto child   = child_options();
        bool emitted = false;
        for (size_t i = 0; i < arg.size; i++) {
            detail::catch_all_variant key;
            detail::catch_all_variant value;
            if (!dec(key)) {
                throw std::runtime_error("Malformed CBOR diagnostic map key");
            }
            if (emitted) {
                append_diagnostic_separator(output_buffer, format_by_rows);
            }
            if (format_by_rows) {
                text::format_to(std::back_inserter(output_buffer), "{}{}", base_offset, std::string(options.row_options.offset, ' '));
            }
            std::visit<void>(diagnostic_visitor{output_buffer, dec, child}, key);
            text::format_to(std::back_inserter(output_buffer), ": ");
            if (!dec(value)) {
                throw std::runtime_error("Malformed CBOR diagnostic map value");
            }
            std::visit<void>(diagnostic_visitor{output_buffer, dec, child}, value);
            emitted = true;
        }
        options.row_options.current_indent--;
        if (format_by_rows && emitted) {
            text::format_to(std::back_inserter(output_buffer), "\n{}", base_offset);
        }
        text::format_to(std::back_inserter(output_buffer), "}}");
    }

    template <IsArrayHeader T> constexpr void operator()(const T &arg) {
        check_depth();
        const bool format_by_rows = options.row_options.format_by_rows && !options.row_options.override_array_by_columns;
        auto       base_offset    = std::string(format_by_rows * options.row_options.offset * options.row_options.current_indent, ' ');
        text::format_to(std::back_inserter(output_buffer), "[{}", format_by_rows ? "\n" : "");
        options.row_options.current_indent++;
        auto child   = child_options();
        bool emitted = false;
        for (size_t i = 0; i < arg.size; i++) {
            detail::catch_all_variant values;
            if (!dec(values)) {
                throw std::runtime_error(text::format("Malformed CBOR diagnostic array item {}", i));
            }
            if (emitted) {
                append_diagnostic_separator(output_buffer, format_by_rows);
            }
            if (format_by_rows) {
                text::format_to(std::back_inserter(output_buffer), "{}{}", base_offset, std::string(options.row_options.offset, ' '));
            }
            std::visit<void>(diagnostic_visitor{output_buffer, dec, child}, values);
            emitted = true;
        }
        options.row_options.current_indent--;
        if (format_by_rows && emitted) {
            text::format_to(std::back_inserter(output_buffer), "\n{}", base_offset);
        }
        text::format_to(std::back_inserter(output_buffer), "]");
    }

    template <IsTextHeader T> constexpr void operator()(const T &arg) {
        auto current_pos  = dec.tell();
        auto after_header = current_pos - arg.size;
        auto range        = std::ranges::subrange(after_header, current_pos);
        auto char_view    = range | std::views::transform([](auto b) { return static_cast<char>(b); });
        if (options.check_tstr_utf8 && !detail::is_valid_utf8(range)) {
            text::format_to(std::back_inserter(output_buffer), "non-utf8({})", arg.size);
            return;
        }
        append_escaped_diagnostic_text(output_buffer, std::ranges::begin(char_view), std::ranges::end(char_view));
    }

    template <IsBinaryHeader T> constexpr void operator()(const T &arg) {

        auto current_pos  = dec.tell();
        auto after_header = current_pos - arg.size;
        auto range        = std::ranges::subrange(after_header, current_pos);
        text::format_to(std::back_inserter(output_buffer), "h'");
        for (const auto value : range) {
            if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, std::byte>) {
                text::format_to(std::back_inserter(output_buffer), "{:02x}", std::to_integer<std::uint8_t>(value));
            } else {
                text::format_to(std::back_inserter(output_buffer), "{:02x}", static_cast<std::uint8_t>(value));
            }
        }
        text::format_to(std::back_inserter(output_buffer), "'");
    }

    template <typename T> constexpr void operator()(const T &arg) {

        if constexpr (IsUnsigned<std::remove_cvref_t<decltype(arg)>>) {
            text::format_to(std::back_inserter(output_buffer), "{}", arg);
        } else if constexpr (IsNegative<std::remove_cvref_t<decltype(arg)>>) {
            text::format_to(std::back_inserter(output_buffer), "{}", detail::negative_diagnostic_text(arg));
        } else if constexpr (IsTagHeader<std::remove_cvref_t<decltype(arg)>>) {
            check_depth();
            detail::catch_all_variant value;
            text::format_to(std::back_inserter(output_buffer), "{}(", arg.tag);
            if (!dec(value)) {
                throw std::runtime_error("Malformed CBOR diagnostic tag payload");
            }
            std::visit<void>(diagnostic_visitor{output_buffer, dec, child_options()}, value);
            text::format_to(std::back_inserter(output_buffer), ")");
        } else if constexpr (IsSimple<std::remove_cvref_t<decltype(arg)>>) {
            if constexpr (IsBool<std::remove_cvref_t<decltype(arg)>>) {
                text::format_to(std::back_inserter(output_buffer), "{}", arg ? "true" : "false");
            } else if constexpr (IsNull<std::remove_cvref_t<decltype(arg)>>) {
                text::format_to(std::back_inserter(output_buffer), "null");
            } else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(arg)>, float16_t> ||
                                 std::is_same_v<std::remove_cvref_t<decltype(arg)>, float>) {
                text::format_to(std::back_inserter(output_buffer), "{}", static_cast<double>(arg));
            } else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(arg)>, double>) {
                text::format_to(std::back_inserter(output_buffer), "{}", arg);
            } else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(arg)>, simple>) {
                if (arg.value == static_cast<simple::value_type>(SimpleType::Undefined)) {
                    text::format_to(std::back_inserter(output_buffer), "undefined");
                } else {
                    text::format_to(std::back_inserter(output_buffer), "simple({})", arg.value);
                }
            } else {
                text::format_to(std::back_inserter(output_buffer), "simple");
            }
        } else {
            text::format_to(std::back_inserter(output_buffer), "unknown");
        }
    }
};

template <typename OutputBuffer, typename Decoder>
auto make_diagnostic_visitor(OutputBuffer &output_buffer, Decoder &dec, DiagnosticOptions options) {
    return diagnostic_visitor<OutputBuffer, Decoder>{output_buffer, dec, options};
}

template <ValidCborBuffer CborBuffer, typename OutputBuffer>
constexpr void buffer_diagnostic(const CborBuffer &buffer, OutputBuffer &output_buffer, DiagnosticOptions options = {}) {
    detail::catch_all_variant values;
    auto                      dec = make_decoder(buffer);

    text::format_to(std::back_inserter(output_buffer), "{}", options.row_options.format_by_rows ? "[\n" : "[");

    bool emitted = false;
    while (!detail::decoder_at_end(dec)) {
        auto result = dec(values);
        if (!result) {
            throw std::runtime_error("Malformed CBOR diagnostic top-level item");
        }
        if (emitted) {
            append_diagnostic_separator(output_buffer, options.row_options.format_by_rows);
        }
        std::visit(make_diagnostic_visitor(output_buffer, dec, options), values);
        emitted = true;
    }

    if (options.row_options.format_by_rows && emitted) {
        text::format_to(std::back_inserter(output_buffer), "\n");
    }
    text::format_to(std::back_inserter(output_buffer), "]");
}

} // namespace cbor::tags
