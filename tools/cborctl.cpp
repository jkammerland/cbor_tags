#include "cbor_tags/extensions/cbor_visualization.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

namespace text = cbor::tags::detail::text_format;

enum class command_mode { annotate, diagnostic };
enum class input_format { hex, base64 };

struct usage_error final : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct input_error final : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[nodiscard]] bool is_ascii_space(char value) noexcept {
    return value == ' ' || value == '\n' || value == '\r' || value == '\t' || value == '\f' || value == '\v';
}

[[nodiscard]] bool starts_with(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

void check_size(std::size_t size, std::size_t additional, std::optional<std::size_t> limit, std::string_view kind) {
    if (limit && (size > *limit || additional > *limit - size)) {
        throw input_error(text::format("{} size limit exceeded", kind));
    }
}

[[nodiscard]] std::string read_stdin(std::optional<std::size_t> limit) {
    std::string            input;
    std::array<char, 4096> chunk{};
    while (std::cin) {
        // Read at most one byte beyond the remaining allowance to detect overflow.
        auto count = chunk.size();
        if (limit && *limit - input.size() < count) {
            count = *limit - input.size() + 1U;
        }
        std::cin.read(chunk.data(), static_cast<std::streamsize>(count));
        const auto received = static_cast<std::size_t>(std::cin.gcount());
        check_size(input.size(), received, limit, "source");
        input.append(chunk.data(), received);
    }
    if (std::cin.bad()) {
        throw input_error("failed to read stdin");
    }
    return input;
}

[[nodiscard]] std::string input_text_from_positionals(const std::vector<std::string_view> &positionals, std::optional<std::size_t> limit) {
    if (positionals.empty() || (positionals.size() == 1U && positionals.front() == "-")) {
        return read_stdin(limit);
    }

    std::string result;
    for (const auto &part : positionals) {
        if (!result.empty()) {
            check_size(result.size(), 1U, limit, "source");
            result.push_back(' ');
        }
        check_size(result.size(), part.size(), limit, "source");
        result.append(part);
    }
    return result;
}

[[nodiscard]] int hex_value(char value) noexcept {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

template <typename Visitor> void visit_hex_digits(std::string_view input, Visitor visitor) {
    auto in_comment = false;
    for (const auto value : input) {
        if (in_comment) {
            if (value == '\n' || value == '\r') {
                in_comment = false;
            }
            continue;
        }
        if (value == '#') {
            in_comment = true;
            continue;
        }
        if (is_ascii_space(value)) {
            continue;
        }
        visitor(value);
    }
}

[[nodiscard]] std::vector<std::byte> decode_hex(std::string_view input, std::optional<std::size_t> limit) {
    auto digit_count = std::size_t{};
    visit_hex_digits(input, [&](char) { ++digit_count; });
    if (digit_count % 2U != 0U) {
        throw input_error("hex input has odd number of digits");
    }

    std::vector<std::byte> output;
    auto                   index = std::size_t{};
    auto                   high  = 0;
    visit_hex_digits(input, [&](char digit) {
        const auto value = hex_value(digit);
        if (value < 0) {
            throw input_error(text::format("hex input contains non-hex character near offset {}", index - index % 2U));
        }
        if (index % 2U == 0U) {
            high = value;
        } else {
            check_size(output.size(), 1U, limit, "input");
            output.push_back(static_cast<std::byte>((high << 4U) | value));
        }
        ++index;
    });
    return output;
}

[[nodiscard]] int base64_value(char value) noexcept {
    if (value >= 'A' && value <= 'Z') {
        return value - 'A';
    }
    if (value >= 'a' && value <= 'z') {
        return value - 'a' + 26;
    }
    if (value >= '0' && value <= '9') {
        return value - '0' + 52;
    }
    if (value == '+' || value == '-') {
        return 62;
    }
    if (value == '/' || value == '_') {
        return 63;
    }
    return -1;
}

void validate_base64_tail_bits(std::size_t data_count, int last, std::size_t padding_count) {
    if (data_count == 0U) {
        if (padding_count != 0U) {
            throw input_error("base64 padding without data");
        }
        return;
    }

    if (data_count % 4U == 1U) {
        throw input_error("base64 input length is invalid");
    }
    if (padding_count == 1U && data_count % 4U != 3U) {
        throw input_error("base64 input has invalid single padding");
    }
    if (padding_count == 2U && data_count % 4U != 2U) {
        throw input_error("base64 input has invalid double padding");
    }

    if ((padding_count == 2U || (padding_count == 0U && data_count % 4U == 2U)) && (last & 0x0F) != 0) {
        throw input_error("base64 input has non-zero trailing bits");
    }
    if ((padding_count == 1U || (padding_count == 0U && data_count % 4U == 3U)) && (last & 0x03) != 0) {
        throw input_error("base64 input has non-zero trailing bits");
    }
}

[[nodiscard]] std::vector<std::byte> decode_base64(std::string_view input, std::optional<std::size_t> limit) {
    auto data_count    = std::size_t{};
    auto padding_count = std::size_t{};
    for (const auto value : input) {
        if (is_ascii_space(value)) {
            continue;
        }
        if (value == '=') {
            ++padding_count;
        } else {
            if (padding_count != 0U) {
                throw input_error("base64 padding must be at the end");
            }
            ++data_count;
        }
    }
    if (padding_count > 2U) {
        throw input_error("base64 padding may contain at most two '=' characters");
    }
    if (padding_count != 0U && (data_count + padding_count) % 4U != 0U) {
        throw input_error("padded base64 length must be a multiple of four");
    }
    if (padding_count == 0U && data_count % 4U == 1U) {
        throw input_error("base64 input length is invalid");
    }

    std::vector<std::byte> output;
    auto                   accumulator = std::uint32_t{};
    auto                   bit_count   = 0U;
    auto                   index       = std::size_t{};
    auto                   last        = 0;
    for (const auto digit : input) {
        if (is_ascii_space(digit) || digit == '=') {
            continue;
        }
        const auto value = base64_value(digit);
        if (value < 0) {
            throw input_error(text::format("base64 input contains invalid character near offset {}", index));
        }
        last = value;
        ++index;
        accumulator = (accumulator << 6U) | static_cast<std::uint32_t>(value);
        bit_count += 6U;
        if (bit_count >= 8U) {
            bit_count -= 8U;
            check_size(output.size(), 1U, limit, "input");
            output.push_back(static_cast<std::byte>((accumulator >> bit_count) & 0xFFU));
            accumulator &= (std::uint32_t{1U} << bit_count) - 1U;
        }
    }
    validate_base64_tail_bits(data_count, last, padding_count);
    return output;
}

[[nodiscard]] std::size_t parse_size(std::string_view value, std::string_view option_name) {
    auto        result      = std::size_t{};
    const auto *begin       = value.data();
    const auto *end         = value.data() + value.size();
    const auto [ptr, error] = std::from_chars(begin, end, result);
    if (error != std::errc{} || ptr != end) {
        throw usage_error(text::format("{} expects an unsigned integer, got '{}'", option_name, value));
    }
    return result;
}

[[nodiscard]] input_format parse_input_format(std::string_view value) {
    if (value == "hex") {
        return input_format::hex;
    }
    if (value == "base64") {
        return input_format::base64;
    }
    throw usage_error("--input expects 'hex' or 'base64'");
}

[[nodiscard]] cbor::tags::AnnotationMode parse_annotation_mode(std::string_view value) {
    if (value == "smart") {
        return cbor::tags::AnnotationMode::smart;
    }
    if (value == "no_annotation") {
        return cbor::tags::AnnotationMode::no_annotation;
    }
    throw usage_error("--mode expects 'smart' or 'no_annotation'");
}

[[nodiscard]] command_mode parse_command(std::string_view value) {
    if (value == "annotate") {
        return command_mode::annotate;
    }
    if (value == "diagnostic") {
        return command_mode::diagnostic;
    }
    throw usage_error("expected subcommand 'annotate' or 'diagnostic'");
}

[[nodiscard]] std::vector<std::byte> decode_input(std::string_view input, input_format format, std::optional<std::size_t> limit) {
    if (format == input_format::hex) {
        return decode_hex(input, limit);
    }
    return decode_base64(input, limit);
}

void print_usage(std::ostream &out, std::string_view executable) {
    out << "Usage:\n"
        << "  " << executable << " annotate --input hex|base64 [DATA|-] [options]\n"
        << "  " << executable << " diagnostic --input hex|base64 [DATA|-] [options]\n\n"
        << "Input:\n"
        << "  --input hex|base64     Required input encoding. No auto-detection.\n"
        << "  DATA                   Input text. If omitted or exactly '-', read stdin.\n"
        << "  --                     End options; use before base64url data starting with '-'.\n\n"
        << "Resource options (both commands):\n"
        << "  --max-source-size N    Input text bytes, including whitespace and comments.\n"
        << "  --max-input-size N     Decoded CBOR bytes.\n"
        << "  --max-output-size N    Rendered bytes, including the final newline.\n"
        << "  No byte limits by default; limits apply only when set.\n\n"
        << "Annotation options:\n"
        << "  --mode smart|no_annotation\n"
        << "  --current-indent N --offset N --annotation-column N\n"
        << "  --indent-width N --comment-indent-width N\n"
        << "  --max-bytes-per-line N Maximum payload bytes per annotation line.\n"
        << "  --max-structure-depth N Maximum annotation nesting depth.\n\n"
        << "Diagnostic options:\n"
        << "  --format-by-rows --no-format-by-rows\n"
        << "  --override-array-by-columns --no-override-array-by-columns\n"
        << "  --row-offset N --row-current-indent N\n"
        << "  --check-tstr-utf8      Mark invalid UTF-8 text in diagnostic output.\n"
        << "  --max-depth N         Maximum diagnostic nesting depth.\n"
        << "  --current-depth N     Initial diagnostic nesting depth.\n";
}

struct parsed_arguments {
    command_mode                  command{};
    std::optional<input_format>   input;
    cbor::tags::AnnotationOptions annotation_options;
    cbor::tags::DiagnosticOptions diagnostic_options;
    std::optional<std::size_t>    max_source_size;
    std::optional<std::size_t>    max_input_size;
    std::optional<std::size_t>    max_output_size;
    std::vector<std::string_view> positionals;
};

[[nodiscard]] std::string_view require_value(int &index, int argc, char *argv[], std::string_view option_name) {
    if (index + 1 >= argc) {
        throw usage_error(text::format("{} requires a value", option_name));
    }
    ++index;
    return argv[index];
}

void parse_common_option(parsed_arguments &parsed, std::string_view option, std::string_view value) {
    if (option == "--input") {
        parsed.input = parse_input_format(value);
        return;
    }
    if (option == "--max-source-size") {
        parsed.max_source_size = parse_size(value, option);
        return;
    }
    if (option == "--max-input-size") {
        parsed.max_input_size = parse_size(value, option);
        return;
    }
    if (option == "--max-output-size") {
        parsed.max_output_size = parse_size(value, option);
        return;
    }
    throw usage_error(text::format("unknown option '{}'", option));
}

void parse_annotation_option(parsed_arguments &parsed, std::string_view option, std::string_view value) {
    auto &options = parsed.annotation_options;
    if (option == "--mode") {
        options.mode = parse_annotation_mode(value);
    } else if (option == "--current-indent") {
        options.current_indent = parse_size(value, option);
    } else if (option == "--offset") {
        options.offset = parse_size(value, option);
    } else if (option == "--max-bytes-per-line") {
        options.max_bytes_per_line = parse_size(value, option);
    } else if (option == "--annotation-column") {
        options.annotation_column = parse_size(value, option);
    } else if (option == "--indent-width") {
        options.indent_width = parse_size(value, option);
    } else if (option == "--comment-indent-width") {
        options.comment_indent_width = parse_size(value, option);
    } else if (option == "--max-structure-depth") {
        options.max_structure_depth = parse_size(value, option);
    } else {
        parse_common_option(parsed, option, value);
    }
}

void parse_diagnostic_option(parsed_arguments &parsed, std::string_view option, std::string_view value) {
    auto &options = parsed.diagnostic_options;
    if (option == "--row-offset") {
        options.row_options.offset = parse_size(value, option);
    } else if (option == "--row-current-indent") {
        options.row_options.current_indent = parse_size(value, option);
    } else if (option == "--max-depth") {
        options.max_depth = parse_size(value, option);
    } else if (option == "--current-depth") {
        options.current_depth = parse_size(value, option);
    } else {
        parse_common_option(parsed, option, value);
    }
}

void parse_value_option(parsed_arguments &parsed, std::string_view option, std::string_view value) {
    if (parsed.command == command_mode::annotate) {
        parse_annotation_option(parsed, option, value);
        return;
    }
    parse_diagnostic_option(parsed, option, value);
}

[[nodiscard]] bool parse_flag_option(parsed_arguments &parsed, std::string_view option) {
    if (parsed.command != command_mode::diagnostic) {
        return false;
    }
    if (option == "--format-by-rows") {
        parsed.diagnostic_options.row_options.format_by_rows = true;
        return true;
    }
    if (option == "--no-format-by-rows") {
        parsed.diagnostic_options.row_options.format_by_rows = false;
        return true;
    }
    if (option == "--override-array-by-columns") {
        parsed.diagnostic_options.row_options.override_array_by_columns = true;
        return true;
    }
    if (option == "--no-override-array-by-columns") {
        parsed.diagnostic_options.row_options.override_array_by_columns = false;
        return true;
    }
    if (option == "--check-tstr-utf8") {
        parsed.diagnostic_options.check_tstr_utf8 = true;
        return true;
    }
    return false;
}

[[nodiscard]] bool option_takes_value(std::string_view option) noexcept {
    return option == "--input" || option == "--mode" || option == "--current-indent" || option == "--offset" || option == "--max-depth" ||
           option == "--max-bytes-per-line" || option == "--annotation-column" || option == "--indent-width" ||
           option == "--comment-indent-width" || option == "--max-structure-depth" || option == "--max-source-size" ||
           option == "--max-input-size" || option == "--max-output-size" || option == "--row-offset" || option == "--row-current-indent" ||
           option == "--current-depth";
}

[[nodiscard]] parsed_arguments parse_arguments(int argc, char *argv[]) {
    if (argc < 2) {
        throw usage_error("missing subcommand");
    }

    auto parsed    = parsed_arguments{};
    parsed.command = parse_command(argv[1]);
    auto end_flags = false;
    for (auto index = 2; index < argc; ++index) {
        const auto arg = std::string_view{argv[index]};
        if (!end_flags && arg == "--") {
            end_flags = true;
            continue;
        }
        if (!end_flags && (arg == "--help" || arg == "-h")) {
            throw usage_error("help requested");
        }
        if (!end_flags && starts_with(arg, "--")) {
            if (parse_flag_option(parsed, arg)) {
                continue;
            }
            if (!option_takes_value(arg)) {
                throw usage_error(text::format("unknown option '{}'", arg));
            }
            parse_value_option(parsed, arg, require_value(index, argc, argv, arg));
            continue;
        }
        parsed.positionals.emplace_back(arg);
    }

    if (!parsed.input.has_value()) {
        throw usage_error("missing required --input hex|base64");
    }
    return parsed;
}

class output_text {
  public:
    using value_type = char;

    explicit output_text(std::optional<std::size_t> limit) : limit_(limit) {}

    void check_visualization_append(std::size_t additional) const { check_size(text_.size(), additional, limit_, "output"); }

    void push_back(char value) {
        check_visualization_append(1U);
        text_.push_back(value);
    }

    [[nodiscard]] bool               empty() const noexcept { return text_.empty(); }
    [[nodiscard]] std::size_t        size() const noexcept { return text_.size(); }
    [[nodiscard]] char               back() const { return text_.back(); }
    [[nodiscard]] const std::string &str() const noexcept { return text_; }

  private:
    std::optional<std::size_t> limit_;
    std::string                text_;
};

[[nodiscard]] int run(int argc, char *argv[]) {
    if (argc >= 2 && (std::string_view{argv[1]} == "--help" || std::string_view{argv[1]} == "-h")) {
        print_usage(std::cout, argv[0]);
        return 0;
    }

    parsed_arguments parsed;
    try {
        parsed = parse_arguments(argc, argv);
    } catch (const usage_error &error) {
        if (std::string_view{error.what()} == "help requested") {
            print_usage(std::cout, argv[0]);
            return 0;
        }
        throw;
    }

    // Smart rendering shares explicitly supplied budgets with the CLI pipeline.
    if (parsed.command == command_mode::annotate && parsed.annotation_options.mode == cbor::tags::AnnotationMode::smart) {
        parsed.annotation_options.max_input_size  = parsed.max_input_size.value_or(parsed.annotation_options.max_input_size);
        parsed.annotation_options.max_output_size = parsed.max_output_size.value_or(parsed.annotation_options.max_output_size);
    }

    const auto input  = input_text_from_positionals(parsed.positionals, parsed.max_source_size);
    const auto buffer = decode_input(input, *parsed.input, parsed.max_input_size);

    output_text output(parsed.max_output_size);
    if (parsed.command == command_mode::annotate) {
        cbor::tags::buffer_annotate(buffer, output, parsed.annotation_options);
    } else {
        cbor::tags::buffer_diagnostic(buffer, output, parsed.diagnostic_options);
    }

    if (!output.empty()) {
        if (output.back() != '\n') {
            output.push_back('\n');
        }
        std::cout << output.str();
    }
    return 0;
}

} // namespace

int main(int argc, char *argv[]) {
    try {
        return run(argc, argv);
    } catch (const usage_error &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    } catch (const input_error &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
