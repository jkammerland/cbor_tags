#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_integer.h"
#include "cbor_tags/cbor_reflection.h"

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
#include <type_traits>
#include <variant>
#include <vector>

namespace cbor::tags {

struct AnnotationOptions {
    bool   diagnostic_data{false};
    size_t indent_level{0};
    size_t offset{0};
    size_t max_depth{std::numeric_limits<size_t>::max()};
};

struct CDDLOptions {
    struct RowOptions {
        bool   format_by_rows{true};
        size_t offset{2};
    } row_options;
    bool always_inline{false};
};

struct DiagnosticOptions {
    struct RowOptions {
        bool   format_by_rows{true};
        bool   override_array_by_columns{false};
        size_t offset{2};
        size_t current_indent{0};
    } row_options;

    bool check_tstr_utf8{false};
};

template <typename T, typename OutputBuffer, typename Context>
auto cddl_to(OutputBuffer &output_buffer, const T &t, CDDLOptions = {}, Context = {});

template <typename T, typename OutputBuffer, typename Context> auto cddl_to(OutputBuffer &output_buffer, CDDLOptions = {});

namespace detail {

struct CDDLContext {
    using definition_cddl_pair = std::pair<std::pmr::string, std::pmr::string>;
    std::array<std::byte, 2000>           buffer;
    std::pmr::monotonic_buffer_resource   memory_resource{buffer.data(), buffer.size()};
    std::pmr::deque<definition_cddl_pair> definitions{&memory_resource};

    template <typename T> bool contains(const T &name) const {
        for (const auto &def : definitions) {
            if (std::equal(name.begin(), name.end(), def.first.begin(), def.first.end())) {
                return true;
            }
        }
        return false;
    }

    void insert(std::pmr::string name, std::pmr::string cddl) { definitions.emplace_back(std::move(name), std::move(cddl)); }

    void clear() {
        definitions.clear();
        memory_resource.release();
    }

    template <typename T, typename Context> void register_type(const T &t, CDDLOptions options, Context context) {
        if constexpr (is_static_tag_t<T>::value || is_dynamic_tag_t<T>) {
            return;
        }

        if constexpr (IsAggregate<T>) {
            if (contains(nameof::nameof_type<T>())) {
                return;
            }
            auto             name = std::pmr::string(nameof::nameof_type<T>(), &memory_resource);
            std::pmr::string cddl(&memory_resource);
            cddl_to(cddl, t, options, context);
            insert(std::move(name), std::move(cddl));
        }
        /* Else do nothing */
    }
};

using catch_all_variant = std::variant<positive, negative, as_text_any, as_bstr_any, as_array_any, as_map_any, as_tag_any, float16_t, float,
                                       double, bool, std::nullptr_t>;

template <typename Iterator> void format_bytes(auto &output_buffer, Iterator begin, Iterator end, AnnotationOptions options = {}) {
    std::string indent(options.indent_level * 2, ' ');
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

template <template <typename...> typename Variant, typename... Ts> constexpr auto getVariantNames(const Variant<Ts...> &&) {
    std::string result;
    ((result += std::string(getName(Ts{})) + " / "), ...);
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

template <typename T> constexpr auto getName(const T &t) {
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
            if (t.has_value())
                return std::string(getName<typename T::value_type>(t.value())) + " / null";
            else {
                using value_type = typename T::value_type;
                value_type dummy{};
                return std::string(getName(dummy)) + " / null";
            }
        } else if constexpr (IsVariant<T>) {
            return getVariantNames(T{});
        } else {
            return nameof::nameof_short_type<T>();
        }
    }
}

template <typename T> constexpr auto getName() { return getName(T{}); }

template <typename T>
concept IsReferenceWrapper = std::is_same_v<T, std::reference_wrapper<typename T::type>>;

template <typename T, typename OutputBuffer, typename Context = detail::CDDLContext>
auto cddl_to(OutputBuffer &output_buffer, const T &t, CDDLOptions options, Context context) {
    bool use_brackets     = false;
    bool use_group        = false;
    auto applier_register = [&](auto &&...args) {
        if constexpr (IsReferenceWrapper<Context>) {
            ((context.get().register_type(args, options, context), ...));
        } else {
            ((context.register_type(args, options, std::ref(context)), ...));
        }
    };
    auto applier_formatter = [&](auto &&...args) {
        int not_has_tag_member = !(HasStaticTag<T> || HasDynamicTag<T>);
        int idx                = not_has_tag_member ? 0 : -1;
        if (options.row_options.format_by_rows) {
            int offset_help = not_has_tag_member;
            ((fmt::format_to(
                 std::back_inserter(output_buffer), "{}{}{}", idx++ < 1 ? "" : (options.row_options.format_by_rows ? ",\n" : ", "),
                 offset_help++ == 0 ? "" : std::string(options.row_options.offset, ' '), not_has_tag_member++ == 0 ? "" : getName(args))),
             ...);
        } else {
            ((fmt::format_to(std::back_inserter(output_buffer), "{}{}", idx++ < 1 ? "" : ", ",
                             not_has_tag_member++ == 0 ? "" : getName(args)),
              ...));
        }

        applier_register(std::forward<decltype(args)>(args)...);
    };

    if constexpr (IsAggregate<T>) {
        const auto &&tuple = to_tuple(t);
        fmt::format_to(std::back_inserter(output_buffer), "{} = ", nameof::nameof_short_type<T>());
        [[maybe_unused]] auto size = std::apply([](auto &&...args) { return sizeof...(args); }, tuple);

        if constexpr (IsTag<T>) {
            fmt::format_to(std::back_inserter(output_buffer), "{}", getTagDef(t));
        }

        use_group    = size > 1 || IsTag<T>;
        use_brackets = IsTag<T> && size > 1;

        if (options.row_options.format_by_rows) {
            fmt::format_to(std::back_inserter(output_buffer), "{}", use_brackets ? "([\n" : (use_group ? "(\n" : ""));
        } else {
            fmt::format_to(std::back_inserter(output_buffer), "{}", use_brackets ? "([" : (use_group ? "(" : ""));
        }

        std::apply(applier_formatter, tuple);
    } else if constexpr (IsTuple<T>) {
        [[maybe_unused]] auto size = std::apply([](auto &&...args) { return sizeof...(args); }, t);

        if constexpr (IsTag<T>) {
            fmt::format_to(std::back_inserter(output_buffer), "{}", getTagDef(t));
        }

        use_group    = size > 2 || IsTag<T>;
        use_brackets = IsTag<T> && size > 2;

        if (options.row_options.format_by_rows) {
            fmt::format_to(std::back_inserter(output_buffer), "{}", use_brackets ? "[\n" : (use_group ? "(\n" : ""));
        } else {
            fmt::format_to(std::back_inserter(output_buffer), "{}", use_brackets ? "[" : (use_group ? "(" : ""));
        }

        if constexpr (IsTag<T>) {
            std::apply(applier_formatter, detail::tuple_tail(t));
        } else {
            std::apply(applier_formatter, t);
        }
    } else {
        fmt::format_to(std::back_inserter(output_buffer), "{} = {}", nameof::nameof_type<T>(), getName(t));
    }

    if (options.row_options.format_by_rows) {
        fmt::format_to(std::back_inserter(output_buffer), "{}", use_brackets ? "\n])" : (use_group ? "\n)" : ""));
    } else {
        fmt::format_to(std::back_inserter(output_buffer), "{}", use_brackets ? "])" : (use_group ? ")" : ""));
    }

    if constexpr (!IsReferenceWrapper<Context>) {
        // Reverse, higher likelyhood of top - down order
        for (const auto &def : context.definitions | std::views::reverse) {
            fmt::format_to(std::back_inserter(output_buffer), "\n{}", def.second);
        }
    }
}

template <typename T, typename OutputBuffer, typename Context = detail::CDDLContext>
auto cddl_to(OutputBuffer &output_buffer, CDDLOptions options) {
    cddl_to(output_buffer, T{}, options);
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
        if constexpr (IsTextHeader<std::remove_cvref_t<decltype(value)>>) {
            return value.size;
        } else if constexpr (IsBinaryHeader<std::remove_cvref_t<decltype(value)>>) {
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

    auto                  it   = dec.reader_.position_;
    [[maybe_unused]] auto span = std::span<const std::byte>{};

    while (dec(value)) {
        auto next_it       = dec.reader_.position_;
        auto should_indent = std::visit(indentation_visitor, value);

        if constexpr (IsContiguous<CborBuffer>) {
            span            = std::span<const std::byte>(reinterpret_cast<const std::byte *>(cbor_buffer.data() + it), next_it - it);
            auto span_begin = span.begin();
            auto span_end   = span.end();

            if (std::holds_alternative<as_text_any>(value) || std::holds_alternative<as_bstr_any>(value)) {
                auto size        = std::visit(string_size_visitor, value);
                auto header_size = string_length_to_header_size(size);
                detail::format_bytes(output_buffer, span_begin, span_begin + 1, options); // Major type
                detail::format_bytes(output_buffer, span_begin + 1, span_begin + header_size,
                                     {.indent_level = 0, .offset = 1}); // extra header
                fmt::format_to(std::back_inserter(output_buffer), "\n");
                options.indent_level++;
                options.offset++;
                detail::format_bytes(output_buffer, span_begin + header_size, span_end, options);
                options.indent_level--;
                options.offset--;
            } else {
                detail::format_bytes(output_buffer, span_begin, span_begin + 1, options);
                detail::format_bytes(output_buffer, span_begin + 1, span_end, {.indent_level = 0, .offset = 1});
            }
        } else {
            if (std::holds_alternative<as_text_any>(value) || std::holds_alternative<as_bstr_any>(value)) {
                auto size        = std::visit(string_size_visitor, value);
                auto header_size = string_length_to_header_size(size);
                detail::format_bytes(output_buffer, it, it + 1, options);                                        // Major type
                detail::format_bytes(output_buffer, it + 1, it + header_size, {.indent_level = 0, .offset = 1}); // extra header
                fmt::format_to(std::back_inserter(output_buffer), "\n");
                options.indent_level++;
                options.offset++;
                detail::format_bytes(output_buffer, it + header_size, next_it, options);
                options.indent_level--;
                options.offset--;
            } else {
                detail::format_bytes(output_buffer, it, it + 1, options);
                detail::format_bytes(output_buffer, it + 1, next_it, {.indent_level = 0, .offset = 1});
            }
        }

        if (!indent_stack.empty()) {
            indent_stack.top()--;

            if (indent_stack.top() == 0) {
                indent_stack.pop();
                options.indent_level--;
            }
        }
        options.indent_level += should_indent;
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

template <typename OutputBuffer, typename Decoder> struct diagnostic_visitor {
    OutputBuffer     &output_buffer;
    Decoder          &dec;
    DiagnosticOptions options;

    template <IsMapHeader T> constexpr void operator()(const T &arg) {
        auto base_offset =
            std::string(options.row_options.offset * options.row_options.current_indent * options.row_options.format_by_rows, ' ');
        fmt::format_to(std::back_inserter(output_buffer), "{{{}", options.row_options.format_by_rows ? "\n" : "");
        options.row_options.current_indent++;
        for (size_t i = 0; i < arg.size; i++) {
            detail::catch_all_variant key;
            detail::catch_all_variant value;
            if (!dec(key) || !dec(value)) {
                break;
            }
            fmt::format_to(std::back_inserter(output_buffer), "{}{}", base_offset, std::string(options.row_options.offset, ' '));
            std::visit(diagnostic_visitor{output_buffer, dec, options}, key);
            fmt::format_to(std::back_inserter(output_buffer), ": ");
            std::visit(diagnostic_visitor{output_buffer, dec, options}, value);
            fmt::format_to(std::back_inserter(output_buffer), "{}", options.row_options.format_by_rows ? ",\n" : ", ");
        }
        options.row_options.current_indent--;
        output_buffer.resize(output_buffer.size() - 2);
        fmt::format_to(std::back_inserter(output_buffer), "{}{}}}", options.row_options.format_by_rows ? "\n" : "", base_offset);
    }

    template <IsArrayHeader T> constexpr void operator()(const T &arg) {
        bool format_by_rows = options.row_options.format_by_rows && !options.row_options.override_array_by_columns;
        auto base_offset    = std::string(format_by_rows * options.row_options.offset * options.row_options.current_indent, ' ');
        fmt::format_to(std::back_inserter(output_buffer), "[{}", format_by_rows ? "\n" : "");
        options.row_options.current_indent++;
        for (size_t i = 0; i < arg.size; i++) {
            detail::catch_all_variant values;
            if (!dec(values)) {
                break;
            }
            fmt::format_to(std::back_inserter(output_buffer), "{}{}", base_offset, std::string(options.row_options.offset, ' '));
            std::visit(diagnostic_visitor{output_buffer, dec, options}, values);
            fmt::format_to(std::back_inserter(output_buffer), "{}", options.row_options.format_by_rows ? ",\n" : ", ");
        }
        options.row_options.current_indent--;
        output_buffer.resize(output_buffer.size() - 2);
        fmt::format_to(std::back_inserter(output_buffer), "{}{}]", base_offset, format_by_rows ? "\n" : "");
    }

    template <IsTextHeader T> constexpr void operator()(const T &arg) {

        auto current_pos  = dec.tell();
        auto after_header = current_pos - arg.size;
        auto range        = std::ranges::subrange(after_header, current_pos);
        auto char_view    = range | std::views::transform([](std::byte b) { return static_cast<char>(b); });
        if (options.check_tstr_utf8) {
            throw std::runtime_error("UTF-8 check not implemented");
        }
        fmt::format_to(std::back_inserter(output_buffer), "\"{}\"", fmt::join(char_view, ""));
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
            detail::catch_all_variant value;
            fmt::format_to(std::back_inserter(output_buffer), "{}(", arg.tag);
            if (!dec(value)) {
                return;
            }
            std::visit(diagnostic_visitor{output_buffer, dec, options}, value);
            fmt::format_to(std::back_inserter(output_buffer), ")");
        } else if constexpr (IsSimple<std::remove_cvref_t<decltype(arg)>>) {
            if constexpr (IsBool<std::remove_cvref_t<decltype(arg)>>) {
                fmt::format_to(std::back_inserter(output_buffer), "{}", arg ? "true" : "false");
            } else if constexpr (IsNull<std::remove_cvref_t<decltype(arg)>>) {
                fmt::format_to(std::back_inserter(output_buffer), "null");
            } else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(arg)>, float16_t>) {
                fmt::format_to(std::back_inserter(output_buffer), "{}", static_cast<double>(arg));
            } else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(arg)>, float>) {
                fmt::format_to(std::back_inserter(output_buffer), "{}", static_cast<double>(arg));
            } else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(arg)>, double>) {
                fmt::format_to(std::back_inserter(output_buffer), "{}", arg);
            } else {
                fmt::format_to(std::back_inserter(output_buffer), "simple");
            }
        } else {
            fmt::format_to(std::back_inserter(output_buffer), "unknown");
        }
    }
};

template <ValidCborBuffer CborBuffer, typename OutputBuffer>
constexpr void diagnostic_buffer(const CborBuffer &buffer, OutputBuffer &output_buffer, DiagnosticOptions options = {}) {
    detail::catch_all_variant values;
    auto                      dec = make_decoder(buffer);

    fmt::format_to(std::back_inserter(output_buffer), "{}", options.row_options.format_by_rows ? "[\n" : "[");

    while (dec(values)) {
        std::visit(diagnostic_visitor{output_buffer, dec, options}, values);
        fmt::format_to(std::back_inserter(output_buffer), "{}", options.row_options.format_by_rows ? ",\n" : ", ");
    }

    // Remove last comma
    output_buffer.resize(output_buffer.size() - 2);
    fmt::format_to(std::back_inserter(output_buffer), "{}", options.row_options.format_by_rows ? "\n]" : "]");
}

} // namespace cbor::tags
