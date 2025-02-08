#include <filesystem>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>
#include <fstream>
#include <iostream>
#include <ranges>
#include <set>
#include <sstream>
#include <string_view>
#include <vector>

void generate_header(fmt::memory_buffer &out, const std::vector<std::pair<int, int>> &ranges) {
    // Find maximum N across all ranges
    int max_N = 0;
    for (const auto &[start, end] : ranges) {
        max_N = std::max(max_N, end);
    }

    constexpr int max_members         = 128;
    bool          disable_size_assert = max_N > max_members;

    // Create a set of all numbers we need to generate and find holes
    std::set<int> numbers_to_generate;
    auto          hint = numbers_to_generate.end();
    for (const auto &[start, end] : ranges) {
        for (auto i : std::views::iota(start, end + 1)) {
            hint = numbers_to_generate.insert(hint, i);
        }
    }

    // Find holes and create static assert conditions
    std::string static_asserts;
    if (disable_size_assert) {
        static_asserts = "// Static asserts disabled due to large member count\n    ";
        fmt::print("Disabling static asserts in to_tuple(...) due to large member count [greater than {}]\n", max_members);
    } else if (ranges.size() > 1) {
        // Check if there are numbers less than the first range
        if (ranges[0].first > 1) {
            static_asserts += fmt::format("static_assert(detail::aggregate_binding_count<type> >= {}, "
                                          "\"Type must have at least {} members\");\n    ",
                                          ranges[0].first, ranges[0].first);
        }

        // Find holes between ranges
        int prev_end = ranges[0].second;
        for (size_t i = 1; i < ranges.size(); ++i) {
            if (ranges[i].first > prev_end + 1) {
                static_asserts += fmt::format("static_assert(detail::aggregate_binding_count<type> <= {} || "
                                              "detail::aggregate_binding_count<type> >= {}, "
                                              "\"Type must have {} or fewer members, or {} or more members\");\n    ",
                                              prev_end, ranges[i].first, prev_end, ranges[i].first);
            }
            prev_end = ranges[i].second;
        }

        // Add final range check
        static_asserts += fmt::format("static_assert(detail::aggregate_binding_count<type> <= {}, "
                                      "\"Type must have no more than {} members\");\n    ",
                                      max_N, max_N);
    } else {
        static_asserts =
            fmt::format("static_assert(detail::aggregate_binding_count<type> <= detail::MAX_REFLECTION_MEMBERS, "
                        "\"Type must have at most {} members. Rerun the generator with a higher value if you need more.\");\n    ",
                        max_N);
    }

    fmt::format_to(std::back_inserter(out), R"(#pragma once

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_detail.h"

#include <tuple>
#include <type_traits>

namespace cbor::tags {{

namespace detail {{
constexpr size_t MAX_REFLECTION_MEMBERS = {0};
}} // namespace detail

template <class T> constexpr auto to_tuple(T &&object) noexcept {{
    using type = std::decay_t<T>;
    static_assert(IsAggregate<type>, "Type must be an aggregate");
    {1}

    if constexpr (IsTuple<type>) {{
        return; // unreachable due to IsAggregate
    }})",
                   max_N, static_asserts);

    // Generate in reverse order
    for (auto i : std::ranges::reverse_view(numbers_to_generate)) {
        std::vector<std::string> params;
        std::vector<std::string> anys;
        params.reserve(i);
        anys.reserve(i);
        for (decltype(i) j = 1; j <= i; ++j) {
            params.push_back(fmt::format("p{}", j));
            anys.emplace_back("any");
        }

        fmt::format_to(std::back_inserter(out), R"( else if constexpr (IsBracesContructible<type, {0}>) {{
        auto &[{1}] = object;
        return std::tie({1});
    }})",
                       fmt::join(anys, ", "), fmt::join(params, ", "));
    }

    fmt::format_to(std::back_inserter(out), R"( else {{
        return std::make_tuple();
    }}
}}

}} // namespace cbor::tags
)");
}

std::vector<std::pair<int, int>> parse_ranges(int argc, char *argv[]) {
    std::vector<std::pair<int, int>> ranges;

    for (int i = 1; i < argc; ++i) {
        std::string        input(argv[i]);
        std::istringstream ss(input);
        std::string        range_str;

        // Split the input string by spaces
        while (ss >> range_str) {
            size_t colon_pos = range_str.find(':');

            if (colon_pos != std::string::npos) {
                // Range format: start:end
                try {
                    int start = std::stoi(range_str.substr(0, colon_pos));
                    int end   = std::stoi(range_str.substr(colon_pos + 1));
                    if (start > 0 && end > 0 && start <= end) {
                        ranges.emplace_back(start, end);
                    } else {
                        throw std::runtime_error("Invalid range values");
                    }
                } catch (const std::exception &) {
                    fmt::print(stderr, "Error: Invalid range format '{}'. Expected format: start:end\n", range_str);
                    exit(1);
                }
            } else {
                // Single number format
                try {
                    int num = std::stoi(range_str);
                    if (num > 0) {
                        ranges.emplace_back(num, num);
                    } else {
                        throw std::runtime_error("Invalid number");
                    }
                } catch (const std::exception &) {
                    fmt::print(stderr, "Error: Invalid number format '{}'\n", range_str);
                    exit(1);
                }
            }
        }
    }

    if (ranges.empty()) {
        fmt::print(stderr, "Usage: {} \"<number> ... or <start:end> ...\"\n", argv[0]);
        exit(1);
    }

    return ranges;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fmt::print(stderr, "Usage: {} <number> ... or <start:end> ...\n", argv[0]);
        return 1;
    }

    auto ranges = parse_ranges(argc, argv);

    std::string   filename = "cbor_reflection_impl.h";
    std::ofstream out_file(filename);

    if (!out_file) {
        fmt::print(stderr, "Error: Failed to open output file.\n");
        return 1;
    }

    fmt::memory_buffer buffer;
    buffer.reserve(1024 * 1024 * ranges.size()); // Reserve initial size for the buffer in megabytes
    generate_header(buffer, ranges);

    out_file << std::string_view(buffer.data(), buffer.size());
    fmt::print("Generated reflection header containing to_tuple(...) at {}/{}. Supported struct size ranges: ",
               std::filesystem::current_path().string(), filename);
    for (const auto &[start, end] : ranges) {
        fmt::print("{}-{} ", start, end);
    }
    fmt::print("\n");

    return 0;
}