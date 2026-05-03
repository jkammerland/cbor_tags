#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_integer.h"
#include "cbor_tags/cbor_reflection.h"
#include "cbor_tags/cbor_tags_config.h"

#include <array>
#include <cbor_tags/cbor_concepts.h>
#include <cstddef>
#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <functional>
#include <iterator>
#include <memory_resource>
#include <nameof.hpp>
#include <optional>
#include <span>
#include <stack>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cbor::tags {

struct AnnotationOptions {
    bool   diagnostic_data{false};
    size_t current_indent{0};
    size_t offset{0};
    size_t max_depth{std::numeric_limits<size_t>::max()};
};

struct CDDLOptions {
    struct RowOptions {
        bool   format_by_rows{true};
        size_t offset{2};
    } row_options;
    bool             always_inline{false};
    std::string_view root_name{};
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

struct CDDLContext;

template <typename T> std::string ensure_cddl_definition(CDDLContext &context, CDDLOptions options, std::string_view preferred_name = {});

struct CDDLContext {
    enum class DefinitionState { visiting, done };

    struct definition_cddl_pair {
        std::pmr::string key;
        std::pmr::string name;
        std::pmr::string cddl;
        DefinitionState  state{DefinitionState::visiting};
        bool             recursive_reference{false};
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

    void clear() { definitions.clear(); }

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
                fmt::format_to(std::back_inserter(output_buffer), "\n");
            }
            fmt::format_to(std::back_inserter(output_buffer), "{}{}", indent, offset);
            is_first_line = false;
        }

        fmt::format_to(std::back_inserter(output_buffer), "{:02x}", static_cast<std::uint8_t>(*begin));
        ++begin;

        ++current_count;
        if (current_count >= bytes_per_line) {
            current_count = 0;
        }
    }
}
} // namespace detail

template <typename T> constexpr auto getName(const T &);
template <typename T> constexpr auto getName();

template <template <typename...> typename Variant, typename... Ts> constexpr auto getVariantNames() {
    std::string result;
    ((result += std::string(getName<Ts>()) + " / "), ...);
    return result.substr(0, result.empty() ? 0 : (result.size() - 3));
}

template <template <typename...> typename Variant, typename... Ts> constexpr auto getVariantNames(const Variant<Ts...> &&) {
    std::string result;
    ((result += std::string(getName<Ts>()) + " / "), ...);
    return result.substr(0, result.empty() ? 0 : (result.size() - 3));
}

template <IsTag T> constexpr auto getTagDef(const T &t) {
    if constexpr (HasInlineTag<T>) {
        return fmt::format("#6.{}", T::cbor_tag);
    } else {
        if constexpr (IsTuple<T>) {
            const auto tag = std::get<0>(t);
            return fmt::format("#6.{}", static_cast<std::uint64_t>(tag));
        } else {
            return fmt::format("#6.{}", static_cast<std::uint64_t>(t.cbor_tag));
        }
    }
}

template <typename T> constexpr auto getName(const T &) {
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
        return nameof::nameof_short_type<T>();
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
        } else if constexpr (IsVariant<T>) {
            return getVariantNames(T{});
        } else {
            return nameof::nameof_short_type<T>();
        }
    }
}

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
        return nameof::nameof_short_type<T>();
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
        } else if constexpr (IsVariant<T>) {
            return getVariantNames(T{});
        } else {
            return nameof::nameof_short_type<T>();
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

template <typename T> struct is_std_span : std::false_type {};
template <typename T, std::size_t Extent> struct is_std_span<std::span<T, Extent>> : std::true_type {
    using value_type                    = T;
    static constexpr std::size_t extent = Extent;
};

template <typename T> using aggregate_tuple_t = std::remove_cvref_t<decltype(to_tuple(std::declval<T &>()))>;

template <typename T, typename...> struct first_type {
    using type = T;
};

template <typename... Ts> using first_type_t = typename first_type<Ts...>::type;

template <typename T> std::string cddl_type_expr(CDDLContext &context, CDDLOptions options);

constexpr bool is_cddl_id_start(char value) {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || value == '_' || value == '$' || value == '@';
}

constexpr bool is_cddl_id_continue(char value) { return is_cddl_id_start(value) || (value >= '0' && value <= '9'); }

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

template <typename T> std::string cddl_type_key() { return std::string(nameof::nameof_full_type<std::remove_cvref_t<T>>()); }

template <typename T> std::string cddl_type_name() { return sanitize_cddl_id(nameof::nameof_short_type<std::remove_cvref_t<T>>()); }

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
        auto candidate = fmt::format("{}_{}", fallback, suffix);
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

template <typename... Ts> std::string cddl_fixed_array_expr(CDDLContext &context, CDDLOptions options) {
    std::array<std::string, sizeof...(Ts)> items{cddl_type_expr<std::remove_cvref_t<Ts>>(context, options)...};

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

template <typename... Ts> std::string cddl_payload_expr(CDDLContext &context, CDDLOptions options) {
    if constexpr (sizeof...(Ts) == 0) {
        return "[]";
    } else if constexpr (sizeof...(Ts) == 1) {
        return cddl_type_expr<std::remove_cvref_t<first_type_t<Ts...>>>(context, options);
    } else {
        return cddl_fixed_array_expr<Ts...>(context, options);
    }
}

template <typename Tuple, std::size_t Offset, std::size_t... Is>
std::string cddl_payload_from_tuple(CDDLContext &context, CDDLOptions options, std::index_sequence<Is...>) {
    return cddl_payload_expr<std::tuple_element_t<Offset + Is, Tuple>...>(context, options);
}

template <typename Tuple, std::size_t Offset> std::string cddl_payload_from_tuple(CDDLContext &context, CDDLOptions options) {
    using tuple_type               = std::remove_cvref_t<Tuple>;
    constexpr std::size_t size     = std::tuple_size_v<tuple_type>;
    constexpr std::size_t payloads = size >= Offset ? size - Offset : 0;
    return cddl_payload_from_tuple<tuple_type, Offset>(context, options, std::make_index_sequence<payloads>{});
}

template <typename T> std::string cddl_tag_prefix() {
    using value_type = std::remove_cvref_t<T>;
    if constexpr (is_static_tag_t<value_type>::value) {
        return fmt::format("#6.{}", value_type::cbor_tag);
    } else {
        return "#6";
    }
}

template <typename T>
constexpr bool is_empty_cddl_aggregate_v =
    IsAggregate<std::remove_cvref_t<T>> && !IsTag<std::remove_cvref_t<T>> && aggregate_binding_count<std::remove_cvref_t<T>> == 0;

template <typename T> std::string cddl_aggregate_expr(CDDLContext &context, CDDLOptions options) {
    using value_type = std::remove_cvref_t<T>;
    static_assert(!is_empty_cddl_aggregate_v<value_type>, "empty aggregate has no CBOR data item shape; CDDL schema unsupported");

    using tuple_type = aggregate_tuple_t<value_type>;

    if constexpr (HasInlineTag<value_type>) {
        return fmt::format("#6.{}({})", value_type::cbor_tag, cddl_payload_from_tuple<tuple_type, 0>(context, options));
    } else if constexpr (HasStaticTag<value_type>) {
        using tag_type = std::remove_cvref_t<decltype(value_type::cbor_tag)>;
        return fmt::format("{}({})", cddl_tag_prefix<tag_type>(), cddl_payload_from_tuple<tuple_type, 1>(context, options));
    } else if constexpr (HasDynamicTag<value_type>) {
        return fmt::format("#6({})", cddl_payload_from_tuple<tuple_type, 1>(context, options));
    } else {
        return cddl_payload_from_tuple<tuple_type, 0>(context, options);
    }
}

template <typename T> std::string cddl_tuple_expr(CDDLContext &context, CDDLOptions options) {
    using value_type = std::remove_cvref_t<T>;
    using tuple_type = value_type;

    if constexpr (IsTaggedTuple<value_type>) {
        using tag_type = std::remove_cvref_t<std::tuple_element_t<0, tuple_type>>;
        return fmt::format("{}({})", cddl_tag_prefix<tag_type>(), cddl_payload_from_tuple<tuple_type, 1>(context, options));
    } else {
        return cddl_payload_from_tuple<tuple_type, 0>(context, options);
    }
}

template <typename T> std::string cddl_sequence_expr(CDDLContext &context, CDDLOptions options) {
    using value_type = std::remove_cvref_t<T>;
    using item_type  = std::remove_cvref_t<typename value_type::value_type>;

    auto item = parenthesize_choice(cddl_type_expr<item_type>(context, options));
    if constexpr (is_std_array<value_type>::value) {
        return fmt::format("[{}*{} {}]", is_std_array<value_type>::size, is_std_array<value_type>::size, item);
    } else if constexpr (is_std_span<value_type>::value) {
        if constexpr (is_std_span<value_type>::extent != std::dynamic_extent) {
            return fmt::format("[{}*{} {}]", is_std_span<value_type>::extent, is_std_span<value_type>::extent, item);
        } else {
            return fmt::format("[* {}]", item);
        }
    } else {
        return fmt::format("[* {}]", item);
    }
}

template <typename T> std::string cddl_map_expr(CDDLContext &context, CDDLOptions options) {
    using value_type  = std::remove_cvref_t<T>;
    using key_type    = std::remove_cvref_t<typename value_type::key_type>;
    using mapped_type = std::remove_cvref_t<typename value_type::mapped_type>;
    auto key          = parenthesize_choice(cddl_type_expr<key_type>(context, options));
    auto value        = parenthesize_choice(cddl_type_expr<mapped_type>(context, options));
    return fmt::format("{{* {} => {}}}", key, value);
}

template <typename T> std::string cddl_rule_expr(CDDLContext &context, CDDLOptions options, std::string_view name) {
    return fmt::format("{} = {}", name, cddl_aggregate_expr<T>(context, options));
}

template <typename T> std::string ensure_cddl_definition(CDDLContext &context, CDDLOptions options, std::string_view preferred_name) {
    using value_type = std::remove_cvref_t<T>;
    if constexpr (IsAggregate<value_type> && !is_static_tag_t<value_type>::value && !is_dynamic_tag_t<value_type>) {
        const auto key = cddl_type_key<value_type>();
        if (auto *def = context.find_by_key(key)) {
            if (def->state == CDDLContext::DefinitionState::visiting) {
                def->recursive_reference = true;
            }
            return std::string(def->name);
        }

        auto  name = unique_cddl_name(context, key, preferred_name.empty() ? cddl_type_name<value_type>() : preferred_name);
        auto &def  = context.reserve(key, name);
        auto  body = cddl_aggregate_expr<value_type>(context, options);
        if (options.always_inline && preferred_name.empty() && !def.recursive_reference) {
            context.erase_by_key(key);
            return body;
        }

        auto cddl = fmt::format("{} = {}", name, body);
        def.cddl  = std::pmr::string(cddl, &context.memory_resource);
        def.state = CDDLContext::DefinitionState::done;
        return std::string(def.name);
    } else {
        return cddl_type_expr<value_type>(context, options);
    }
}

template <typename T> std::string cddl_type_expr(CDDLContext &context, CDDLOptions options) {
    using value_type = std::remove_cvref_t<T>;
    if constexpr (IsUnsigned<value_type> || IsEnumUnsigned<value_type>) {
        return "uint";
    }
    if constexpr (IsNegative<value_type>) {
        return "nint";
    }
    if constexpr (IsSigned<value_type> || IsEnumSigned<value_type>) {
        return "int";
    }
    if constexpr (IsTextString<value_type>) {
        return "tstr";
    }
    if constexpr (IsBinaryString<value_type>) {
        return "bstr";
    }
    if constexpr (IsIndefiniteWrapper<value_type>) {
        return cddl_type_expr<indefinite_value_t<value_type>>(context, options);
    }
    if constexpr (IsOptional<value_type>) {
        return fmt::format("{} / null", cddl_type_expr<typename value_type::value_type>(context, options));
    }
    if constexpr (IsVariant<value_type>) {
        return []<typename... Ts>(std::variant<Ts...> *, CDDLContext &variant_context, CDDLOptions variant_options) {
            return join_cddl(std::array<std::string, sizeof...(Ts)>{cddl_type_expr<Ts>(variant_context, variant_options)...}, " / ");
        }(static_cast<value_type *>(nullptr), context, options);
    }
    if constexpr (IsArrayHeader<value_type>) {
        return "[* any]";
    }
    if constexpr (IsMapHeader<value_type>) {
        return "{* any => any}";
    }
    if constexpr (IsTagHeader<value_type>) {
        return "#6(any)";
    }
    if constexpr (IsMap<value_type>) {
        return cddl_map_expr<value_type>(context, options);
    }
    if constexpr (IsArray<value_type>) {
        return cddl_sequence_expr<value_type>(context, options);
    }
    if constexpr (is_static_tag_t<value_type>::value || is_dynamic_tag_t<value_type>) {
        return cddl_tag_prefix<value_type>();
    }
    if constexpr (IsTuple<value_type>) {
        return cddl_tuple_expr<value_type>(context, options);
    }
    if constexpr (IsAggregate<value_type>) {
        return ensure_cddl_definition<value_type>(context, options);
    }
    if constexpr (IsSimple<value_type>) {
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
            return "#7";
        }
    }
    return cddl_type_name<value_type>();
}

template <typename Context> decltype(auto) cddl_context_ref(Context &context) {
    if constexpr (IsReferenceWrapper<Context>) {
        return context.get();
    } else {
        return (context);
    }
}

template <typename T> std::string root_rule_name(CDDLOptions options) {
    using value_type = std::remove_cvref_t<T>;
    if (!options.root_name.empty()) {
        return sanitize_cddl_id(options.root_name);
    } else if constexpr (IsAggregate<value_type> && !is_static_tag_t<value_type>::value && !is_dynamic_tag_t<value_type>) {
        return cddl_type_name<value_type>();
    } else {
        return "root";
    }
}

template <typename T> std::string tag_marker_root_expr(CDDLContext &context, CDDLOptions options) {
    (void)context;
    (void)options;
    return fmt::format("{}(any)", cddl_tag_prefix<std::remove_cvref_t<T>>());
}

} // namespace detail

template <typename T, typename OutputBuffer, typename Context = detail::CDDLContext>
auto cddl_schema_to(OutputBuffer &output_buffer, CDDLOptions options, Context context) {
    using value_type   = std::remove_cvref_t<T>;
    auto &cddl_context = detail::cddl_context_ref(context);
    debug::println("cddl_schema_to: {}", nameof::nameof_short_type<T>());

    if constexpr (IsAggregate<value_type> && !is_static_tag_t<value_type>::value && !is_dynamic_tag_t<value_type>) {
        static_assert(!detail::is_empty_cddl_aggregate_v<value_type>,
                      "empty aggregate has no CBOR data item shape; CDDL schema unsupported");
        const auto root_name =
            detail::ensure_cddl_definition<value_type>(cddl_context, options, detail::root_rule_name<value_type>(options));
        const auto  root_key = detail::cddl_type_key<value_type>();
        const auto *root_def = cddl_context.find_by_key(root_key);
        if (root_def != nullptr) {
            fmt::format_to(std::back_inserter(output_buffer), "{}", root_def->cddl);
        } else {
            fmt::format_to(std::back_inserter(output_buffer), "{} = {}", root_name,
                           detail::cddl_aggregate_expr<value_type>(cddl_context, options));
        }
    } else if constexpr (is_static_tag_t<value_type>::value || is_dynamic_tag_t<value_type>) {
        fmt::format_to(std::back_inserter(output_buffer), "{} = {}", detail::root_rule_name<value_type>(options),
                       detail::tag_marker_root_expr<value_type>(cddl_context, options));
    } else {
        const auto root_name          = detail::root_rule_name<value_type>(options);
        const auto root_key           = fmt::format("__cddl_root:{}", root_name);
        bool       reserved_root_name = false;
        if (!cddl_context.contains_name(root_name)) {
            (void)cddl_context.reserve(root_key, root_name);
            reserved_root_name = true;
        }
        auto root_expr = detail::cddl_type_expr<value_type>(cddl_context, options);
        if (reserved_root_name) {
            cddl_context.erase_by_key(root_key);
        }
        fmt::format_to(std::back_inserter(output_buffer), "{} = {}", root_name, root_expr);
    }

    if constexpr (!IsReferenceWrapper<Context>) {
        const auto root_key = [] {
            if constexpr (IsAggregate<value_type> && !is_static_tag_t<value_type>::value && !is_dynamic_tag_t<value_type>) {
                return detail::cddl_type_key<value_type>();
            } else {
                return std::string{};
            }
        }();

        for (const auto &def : cddl_context.definitions | std::views::reverse) {
            const std::string_view key{def.key.data(), def.key.size()};
            if (key != root_key && def.state == detail::CDDLContext::DefinitionState::done) {
                fmt::format_to(std::back_inserter(output_buffer), "\n{}", def.cddl);
            }
        }
    }
}

template <typename CborBuffer, typename OutputBuffer>
auto buffer_annotate(const CborBuffer &cbor_buffer, OutputBuffer &output_buffer, AnnotationOptions options = {}) {
    if (cbor_buffer.empty()) {
        return;
    }
    if (options.diagnostic_data) {
        throw std::runtime_error("Diagnostic data not supported");
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
        } else if (length < 0xFF) {
            return 2;
        } else if (length < 0xFFFF) {
            return 3;
        } else if (length < 0xFFFFFFFF) {
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
            fmt::format_to(std::back_inserter(output_buffer), "\n");
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
        fmt::format_to(std::back_inserter(output_buffer), "\n");
        it = next_it;
    }
}

template <typename OutputBuffer> constexpr void cddl_prelude_to(OutputBuffer &buffer) {
    fmt::format_to(std::back_inserter(buffer), "any = #\n"
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
    fmt::format_to(std::back_inserter(output_buffer), "{}", format_by_rows ? ",\n" : ", ");
}

template <typename OutputBuffer, typename Iterator>
void append_escaped_diagnostic_text(OutputBuffer &output_buffer, Iterator begin, Iterator end) {
    fmt::format_to(std::back_inserter(output_buffer), "\"");
    for (; begin != end; ++begin) {
        const auto value = static_cast<unsigned char>(*begin);
        switch (value) {
        case '"': fmt::format_to(std::back_inserter(output_buffer), "\\\""); break;
        case '\\': fmt::format_to(std::back_inserter(output_buffer), "\\\\"); break;
        case '\n': fmt::format_to(std::back_inserter(output_buffer), "\\n"); break;
        case '\r': fmt::format_to(std::back_inserter(output_buffer), "\\r"); break;
        case '\t': fmt::format_to(std::back_inserter(output_buffer), "\\t"); break;
        default:
            if (value < 0x20) {
                fmt::format_to(std::back_inserter(output_buffer), "\\x{:02x}", value);
            } else {
                fmt::format_to(std::back_inserter(output_buffer), "{}", static_cast<char>(value));
            }
            break;
        }
    }
    fmt::format_to(std::back_inserter(output_buffer), "\"");
}

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
        fmt::format_to(std::back_inserter(output_buffer), "{{{}", options.row_options.format_by_rows ? "\n" : "");
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
                fmt::format_to(std::back_inserter(output_buffer), "{}{}", base_offset, std::string(options.row_options.offset, ' '));
            }
            std::visit<void>(diagnostic_visitor{output_buffer, dec, child}, key);
            fmt::format_to(std::back_inserter(output_buffer), ": ");
            if (!dec(value)) {
                throw std::runtime_error("Malformed CBOR diagnostic map value");
            }
            std::visit<void>(diagnostic_visitor{output_buffer, dec, child}, value);
            emitted = true;
        }
        options.row_options.current_indent--;
        if (format_by_rows && emitted) {
            fmt::format_to(std::back_inserter(output_buffer), "\n{}", base_offset);
        }
        fmt::format_to(std::back_inserter(output_buffer), "}}");
    }

    template <IsArrayHeader T> constexpr void operator()(const T &arg) {
        check_depth();
        const bool format_by_rows = options.row_options.format_by_rows && !options.row_options.override_array_by_columns;
        auto       base_offset    = std::string(format_by_rows * options.row_options.offset * options.row_options.current_indent, ' ');
        fmt::format_to(std::back_inserter(output_buffer), "[{}", format_by_rows ? "\n" : "");
        options.row_options.current_indent++;
        auto child   = child_options();
        bool emitted = false;
        for (size_t i = 0; i < arg.size; i++) {
            detail::catch_all_variant values;
            if (!dec(values)) {
                throw std::runtime_error(fmt::format("Malformed CBOR diagnostic array item {}", i));
            }
            if (emitted) {
                append_diagnostic_separator(output_buffer, format_by_rows);
            }
            if (format_by_rows) {
                fmt::format_to(std::back_inserter(output_buffer), "{}{}", base_offset, std::string(options.row_options.offset, ' '));
            }
            std::visit<void>(diagnostic_visitor{output_buffer, dec, child}, values);
            emitted = true;
        }
        options.row_options.current_indent--;
        if (format_by_rows && emitted) {
            fmt::format_to(std::back_inserter(output_buffer), "\n{}", base_offset);
        }
        fmt::format_to(std::back_inserter(output_buffer), "]");
    }

    template <IsTextHeader T> constexpr void operator()(const T &arg) {
        auto current_pos  = dec.tell();
        auto after_header = current_pos - arg.size;
        auto range        = std::ranges::subrange(after_header, current_pos);
        auto char_view    = range | std::views::transform([](auto b) { return static_cast<char>(b); });
        if (options.check_tstr_utf8) {
            throw std::runtime_error("UTF-8 check not implemented");
        }
        append_escaped_diagnostic_text(output_buffer, std::ranges::begin(char_view), std::ranges::end(char_view));
    }

    template <IsBinaryHeader T> constexpr void operator()(const T &arg) {

        auto current_pos  = dec.tell();
        auto after_header = current_pos - arg.size;
        auto range        = std::ranges::subrange(after_header, current_pos);
        fmt::format_to(std::back_inserter(output_buffer), "h'{:02x}'", fmt::join(range, ""));
    }

    template <typename T> constexpr void operator()(const T &arg) {

        if constexpr (IsUnsigned<std::remove_cvref_t<decltype(arg)>>) {
            fmt::format_to(std::back_inserter(output_buffer), "{}", arg);
        } else if constexpr (IsNegative<std::remove_cvref_t<decltype(arg)>>) {
            fmt::format_to(std::back_inserter(output_buffer), "-{}", arg.value);
        } else if constexpr (IsTagHeader<std::remove_cvref_t<decltype(arg)>>) {
            check_depth();
            detail::catch_all_variant value;
            fmt::format_to(std::back_inserter(output_buffer), "{}(", arg.tag);
            if (!dec(value)) {
                throw std::runtime_error("Malformed CBOR diagnostic tag payload");
            }
            std::visit<void>(diagnostic_visitor{output_buffer, dec, child_options()}, value);
            fmt::format_to(std::back_inserter(output_buffer), ")");
        } else if constexpr (IsSimple<std::remove_cvref_t<decltype(arg)>>) {
            if constexpr (IsBool<std::remove_cvref_t<decltype(arg)>>) {
                fmt::format_to(std::back_inserter(output_buffer), "{}", arg ? "true" : "false");
            } else if constexpr (IsNull<std::remove_cvref_t<decltype(arg)>>) {
                fmt::format_to(std::back_inserter(output_buffer), "null");
            } else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(arg)>, float16_t> ||
                                 std::is_same_v<std::remove_cvref_t<decltype(arg)>, float>) {
                fmt::format_to(std::back_inserter(output_buffer), "{}", static_cast<double>(arg));
            } else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(arg)>, double>) {
                fmt::format_to(std::back_inserter(output_buffer), "{}", arg);
            } else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(arg)>, simple>) {
                if (arg.value == static_cast<simple::value_type>(SimpleType::Undefined)) {
                    fmt::format_to(std::back_inserter(output_buffer), "undefined");
                } else {
                    fmt::format_to(std::back_inserter(output_buffer), "simple");
                }
            } else {
                fmt::format_to(std::back_inserter(output_buffer), "simple");
            }
        } else {
            fmt::format_to(std::back_inserter(output_buffer), "unknown");
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

    fmt::format_to(std::back_inserter(output_buffer), "{}", options.row_options.format_by_rows ? "[\n" : "[");

    bool emitted = false;
    while (!dec.reader_.empty(dec.data_)) {
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
        fmt::format_to(std::back_inserter(output_buffer), "\n");
    }
    fmt::format_to(std::back_inserter(output_buffer), "]");
}

} // namespace cbor::tags
