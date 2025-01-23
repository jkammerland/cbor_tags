#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_integer.h"

#include <cbor_tags/cbor_concepts.h>
#include <cstddef>
#include <fmt/base.h>
#include <fmt/format.h>
#include <iterator>
#include <nameof.hpp>
#include <span>
#include <stack>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace cbor::tags {

struct FormattingOptions {
    bool   diagnostic_data{false};
    size_t indent_level{0};
    size_t offset{0};
    size_t max_depth{std::numeric_limits<size_t>::max()};
};

namespace detail {

using catch_all_variant = std::variant<positive, negative, as_text_any, as_bstr_any, as_array_any, as_map_any, as_tag_any, float16_t, float,
                                       double, bool, std::nullptr_t>;

// Helper function to format bytes between iterators
template <typename Iterator> void format_bytes(auto &output_buffer, Iterator begin, Iterator end, FormattingOptions options = {}) {
    std::string indent(options.indent_level * 2, ' ');
    std::string offset(options.offset, ' ');
    fmt::format_to(std::back_inserter(output_buffer), "{}{}", indent, offset);

    while (begin != end) {
        fmt::format_to(std::back_inserter(output_buffer), "{:02x}", static_cast<std::uint8_t>(*begin));
        ++begin;
    }
}
} // namespace detail

template <typename OutputBuffer, typename... T> constexpr auto CDDL(OutputBuffer &, T &&...) {}

template <typename CborBuffer, typename OutputBuffer>
auto annotate(const CborBuffer &cbor_buffer, OutputBuffer &output_buffer, FormattingOptions options = {}) {
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
} // namespace cbor::tags
