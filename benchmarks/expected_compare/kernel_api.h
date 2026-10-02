#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct expected_comparison_sample {
    std::uint64_t value;
    bool          fail;
};

using expected_comparison_kernel = std::uint64_t (*)(const expected_comparison_sample *, std::size_t);

extern "C" {
std::uint64_t cmp_void_materialized(const expected_comparison_sample *, std::size_t);
std::uint64_t cmp_scalar_return(const expected_comparison_sample *, std::size_t);
std::uint64_t cmp_monadic_chain(const expected_comparison_sample *, std::size_t);
std::uint64_t cmp_scalar_assignment(const expected_comparison_sample *, std::size_t);
std::uint64_t cmp_string_return(const expected_comparison_sample *, std::size_t);
std::uint64_t cmp_string_transition(const expected_comparison_sample *, std::size_t);
std::uint64_t cmp_checked_value(const expected_comparison_sample *, std::size_t);
std::uint64_t cmp_encode_integers(const std::vector<std::uint64_t> &, std::vector<std::byte> &);
std::uint64_t cmp_decode_integers(const std::vector<std::byte> &, std::vector<std::uint64_t> &);
std::uint64_t cmp_decode_truncated(const std::vector<std::byte> &);
}
