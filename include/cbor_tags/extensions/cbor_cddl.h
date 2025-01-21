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
    bool   pretty_print{false};
    size_t indent_level{0};
};

namespace detail {

using catch_all_variant = std::variant<positive, negative, as_text_any, as_bstr_any, as_array_any, as_map_any, as_tag_any, float16_t, float,
                                       double, bool, std::nullptr_t>;

// Helper function to format bytes between iterators
template <typename Iterator> void format_bytes(auto &output_buffer, Iterator begin, Iterator end, FormattingOptions options = {}) {
    std::string indent(options.indent_level * 2, ' ');
    fmt::format_to(std::back_inserter(output_buffer), "{}", indent);
    while (begin != end) {
        fmt::format_to(std::back_inserter(output_buffer), "{:02x}", static_cast<std::uint8_t>(*begin));
        ++begin;
    }
}
} // namespace detail

template <typename OutputBuffer, typename... T> constexpr auto CDDL(OutputBuffer &, T &&...) {}

template <typename CborBuffer, typename OutputBuffer> auto annotate(const CborBuffer &cbor_buffer, OutputBuffer &output_buffer) {
    if (std::size(cbor_buffer) % 2 != 0) {
        throw std::invalid_argument("CBOR buffer must have an even number of bytes");
        return;
    }

    auto dec = make_decoder(cbor_buffer);

    detail::catch_all_variant value;
    FormattingOptions         options;
    std::stack<size_t>        indent_stack;

    auto indentation_visitor = [&indent_stack](auto &&value) {
        if constexpr (IsTextHeader<std::remove_cvref_t<decltype(value)>>) {
            indent_stack.push(value.size);
            return true;
        } else if constexpr (IsBinaryHeader<std::remove_cvref_t<decltype(value)>>) {
            indent_stack.push(value.size);
            return true;
        } else if constexpr (IsArrayHeader<std::remove_cvref_t<decltype(value)>>) {
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

    auto it = dec.reader_.position_;
    while (dec(value)) {
        auto next_it       = dec.reader_.position_;
        auto should_indent = std::visit(indentation_visitor, value);

        if constexpr (IsContiguous<CborBuffer>) {
            auto span = std::span(cbor_buffer.begin() + it, next_it - it);
            detail::format_bytes(output_buffer, span.begin(), span.end(), options);
        } else {
            detail::format_bytes(output_buffer, it, next_it, options);
            if (std::holds_alternative<as_text_any>(value) || std::holds_alternative<as_bstr_any>(value)) {
                auto size = std::visit(string_size_visitor, value);
                fmt::format_to(std::back_inserter(output_buffer), "\n{}{}", std::string(options.indent_level * 2, ' '), size);
                std::advance(dec.reader_.position_, size);
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
        fmt::format_to(std::back_inserter(output_buffer), "\n");
        it = next_it;
    }
}
} // namespace cbor::tags
