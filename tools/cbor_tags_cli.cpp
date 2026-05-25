#include "cbor_tags/extensions/cbor_visualization.h"

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

[[nodiscard]] std::string read_stdin() {
    std::string input{std::istreambuf_iterator<char>{std::cin}, std::istreambuf_iterator<char>{}};
    if (std::cin.bad()) {
        throw input_error("failed to read stdin");
    }
    return input;
}

[[nodiscard]] std::string input_text_from_positionals(const std::vector<std::string> &positionals) {
    if (positionals.empty() || (positionals.size() == 1U && positionals.front() == "-")) {
        return read_stdin();
    }

    std::string result;
    for (const auto &part : positionals) {
        if (!result.empty()) {
            result.push_back(' ');
        }
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

[[nodiscard]] std::string strip_hex_comments_and_ws(std::string_view input) {
    std::string result;
    result.reserve(input.size());

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
        result.push_back(value);
    }
    return result;
}

[[nodiscard]] std::vector<std::byte> decode_hex(std::string_view input) {
    const auto cleaned = strip_hex_comments_and_ws(input);
    if (cleaned.size() % 2U != 0U) {
        throw input_error("hex input has odd number of digits");
    }

    std::vector<std::byte> output;
    output.reserve(cleaned.size() / 2U);
    for (auto index = std::size_t{}; index < cleaned.size(); index += 2U) {
        const auto high = hex_value(cleaned[index]);
        const auto low  = hex_value(cleaned[index + 1U]);
        if (high < 0 || low < 0) {
            throw input_error(text::format("hex input contains non-hex character near offset {}", index));
        }
        output.push_back(static_cast<std::byte>((high << 4U) | low));
    }
    return output;
}

[[nodiscard]] std::string strip_base64_ws(std::string_view input) {
    std::string result;
    result.reserve(input.size());
    for (const auto value : input) {
        if (!is_ascii_space(value)) {
            result.push_back(value);
        }
    }
    return result;
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

void validate_base64_padding(std::string_view input, std::size_t padding_begin) {
    for (auto index = padding_begin; index < input.size(); ++index) {
        if (input[index] != '=') {
            throw input_error("base64 padding must be at the end");
        }
    }

    const auto padding_count = input.size() - padding_begin;
    if (padding_count > 2U) {
        throw input_error("base64 padding may contain at most two '=' characters");
    }
    if (input.size() % 4U != 0U) {
        throw input_error("padded base64 length must be a multiple of four");
    }
}

void validate_base64_tail_bits(const std::vector<int> &values, std::size_t padding_count) {
    if (values.empty()) {
        if (padding_count != 0U) {
            throw input_error("base64 padding without data");
        }
        return;
    }

    const auto data_count = values.size();
    if (data_count % 4U == 1U) {
        throw input_error("base64 input length is invalid");
    }
    if (padding_count == 1U && data_count % 4U != 3U) {
        throw input_error("base64 input has invalid single padding");
    }
    if (padding_count == 2U && data_count % 4U != 2U) {
        throw input_error("base64 input has invalid double padding");
    }

    const auto last = values.back();
    if ((padding_count == 2U || (padding_count == 0U && data_count % 4U == 2U)) && (last & 0x0F) != 0) {
        throw input_error("base64 input has non-zero trailing bits");
    }
    if ((padding_count == 1U || (padding_count == 0U && data_count % 4U == 3U)) && (last & 0x03) != 0) {
        throw input_error("base64 input has non-zero trailing bits");
    }
}

[[nodiscard]] std::vector<std::byte> decode_base64(std::string_view input) {
    const auto cleaned       = strip_base64_ws(input);
    const auto padding_begin = cleaned.find('=');
    if (padding_begin != std::string::npos) {
        validate_base64_padding(cleaned, padding_begin);
    } else if (cleaned.size() % 4U == 1U) {
        throw input_error("base64 input length is invalid");
    }

    const auto padding_count = padding_begin == std::string::npos ? std::size_t{} : cleaned.size() - padding_begin;
    const auto data_count    = cleaned.size() - padding_count;

    std::vector<int> values;
    values.reserve(data_count);
    for (auto index = std::size_t{}; index < data_count; ++index) {
        const auto value = base64_value(cleaned[index]);
        if (value < 0) {
            throw input_error(text::format("base64 input contains invalid character near offset {}", index));
        }
        values.push_back(value);
    }

    validate_base64_tail_bits(values, padding_count);

    std::vector<std::byte> output;
    output.reserve((values.size() * 3U) / 4U);

    auto accumulator = std::uint32_t{};
    auto bit_count   = 0U;
    for (const auto value : values) {
        accumulator = (accumulator << 6U) | static_cast<std::uint32_t>(value);
        bit_count += 6U;
        if (bit_count >= 8U) {
            bit_count -= 8U;
            output.push_back(static_cast<std::byte>((accumulator >> bit_count) & 0xFFU));
            accumulator &= (std::uint32_t{1U} << bit_count) - 1U;
        }
    }

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

[[nodiscard]] std::vector<std::byte> decode_input(std::string_view input, input_format format) {
    if (format == input_format::hex) {
        return decode_hex(input);
    }
    return decode_base64(input);
}

template <cbor::tags::ValidCborBuffer CborBuffer> void validate_cbor_sequence(const CborBuffer &buffer) {
    cbor::tags::detail::catch_all_variant value;
    auto                                  dec = cbor::tags::make_decoder(buffer);

    while (!dec.reader_.empty(dec.data_)) {
        auto result = dec(value);
        if (!result) {
            throw std::runtime_error("Malformed CBOR input");
        }
    }
}

void print_usage(std::ostream &out, std::string_view executable) {
    out << "Usage:\n"
        << "  " << executable << " annotate --input hex|base64 [DATA|-] [options]\n"
        << "  " << executable << " diagnostic --input hex|base64 [DATA|-] [options]\n\n"
        << "Input:\n"
        << "  --input hex|base64     Required input encoding. No auto-detection.\n"
        << "  DATA                   Input text. If omitted or exactly '-', read stdin.\n"
        << "  --                     End options; use before base64url data starting with '-'.\n\n"
        << "Annotation options:\n"
        << "  --mode smart|no_annotation\n"
        << "  --current-indent N --offset N --max-depth N --annotation-column N\n"
        << "  --indent-width N --comment-indent-width N --max-structure-depth N\n"
        << "  --max-input-size N --max-output-size N\n\n"
        << "Diagnostic options:\n"
        << "  --format-by-rows --no-format-by-rows\n"
        << "  --override-array-by-columns --no-override-array-by-columns\n"
        << "  --row-offset N --row-current-indent N --check-tstr-utf8\n"
        << "  --max-depth N --current-depth N\n";
}

struct parsed_arguments {
    command_mode                  command{};
    std::optional<input_format>   input;
    cbor::tags::AnnotationOptions annotation_options;
    cbor::tags::DiagnosticOptions diagnostic_options;
    std::vector<std::string>      positionals;
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
    } else if (option == "--max-depth") {
        options.max_depth = parse_size(value, option);
    } else if (option == "--annotation-column") {
        options.annotation_column = parse_size(value, option);
    } else if (option == "--indent-width") {
        options.indent_width = parse_size(value, option);
    } else if (option == "--comment-indent-width") {
        options.comment_indent_width = parse_size(value, option);
    } else if (option == "--max-structure-depth") {
        options.max_structure_depth = parse_size(value, option);
    } else if (option == "--max-input-size") {
        options.max_input_size = parse_size(value, option);
    } else if (option == "--max-output-size") {
        options.max_output_size = parse_size(value, option);
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
           option == "--annotation-column" || option == "--indent-width" || option == "--comment-indent-width" ||
           option == "--max-structure-depth" || option == "--max-input-size" || option == "--max-output-size" || option == "--row-offset" ||
           option == "--row-current-indent" || option == "--current-depth";
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

    const auto input  = input_text_from_positionals(parsed.positionals);
    const auto buffer = decode_input(input, *parsed.input);

    std::string output;
    if (parsed.command == command_mode::annotate) {
        if (parsed.annotation_options.mode == cbor::tags::AnnotationMode::no_annotation) {
            validate_cbor_sequence(buffer);
        }
        cbor::tags::buffer_annotate(buffer, output, parsed.annotation_options);
    } else {
        cbor::tags::buffer_diagnostic(buffer, output, parsed.diagnostic_options);
    }

    if (!output.empty()) {
        std::cout << output;
        if (output.back() != '\n') {
            std::cout << '\n';
        }
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
