#include <filesystem>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

void generate_header(fmt::memory_buffer &out, int N) {
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
    static_assert(!IsNonAggregate<type>, "Type must be an aggregate");
    static_assert(detail::aggregate_binding_count<type> <= detail::MAX_REFLECTION_MEMBERS, "Type must have at most {0} members. Rerun the generator with a higher value if you need more.");

    if constexpr (IsTuple<type>) {{
        return object;
    }})",
                   N);

    for (int i = N; i > 0; --i) {
        std::vector<std::string> params;
        std::vector<std::string> anys;
        params.reserve(i);
        anys.reserve(i);
        for (int j = 1; j <= i; ++j) {
            params.push_back(fmt::format("p{}", j));
            anys.emplace_back("any");
        }

        fmt::format_to(std::back_inserter(out), R"( else if constexpr (IsBracesContructible<type, {0}>) {{
        auto &&[{1}] = object;
        return std::make_tuple({1});
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

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fmt::print(stderr, "Usage: {} <max_members>\n", argv[0]);
        return 1;
    }

    int N = 0;
    try {
        N = std::stoi(argv[1]);
    } catch (const std::exception &) {
        fmt::print(stderr, "Error: max_members must be a valid integer.\n");
        return 1;
    }

    if (N <= 0) {
        fmt::print(stderr, "Error: max_members must be a positive integer.\n");
        return 1;
    }

    std::string   filename = fmt::format("cbor_reflection_impl.h");
    std::ofstream out_file(filename);

    if (!out_file) {
        fmt::print(stderr, "Error: Failed to open output file.\n");
        return 1;
    }

    fmt::memory_buffer buffer;
    generate_header(buffer, N);

    out_file << std::string_view(buffer.data(), buffer.size());
    fmt::print("Header file '{}' has been generated at {}.\n", filename, std::filesystem::current_path().string());

    return 0;
}