#include "kernel_api.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/expected.h>
#include <string>
#include <utility>

namespace {

using namespace cbor::tags;

#if defined(__GNUC__) || defined(__clang__)
#define CMP_KERNEL __attribute__((noinline, section(".expected_cmp"), aligned(16)))
#else
#error "This comparison requires GCC or Clang compiler barriers and noinline kernels"
#endif

template <typename T> void materialize(const T &value) { asm volatile("" : : "g"(&value) : "memory"); }

CMP_KERNEL expected<std::uint64_t, status_code> scalar_result(std::uint64_t value, bool fail) {
    if (fail) {
        return unexpected<status_code>{status_code::error};
    }
    return value;
}

CMP_KERNEL expected<std::string, status_code> string_result(std::uint64_t value, bool fail) {
    if (fail) {
        return unexpected<status_code>{status_code::error};
    }
    return std::string(32, static_cast<char>('a' + value % 26));
}

} // namespace

extern "C" {

CMP_KERNEL std::uint64_t cmp_void_materialized(const expected_comparison_sample *samples, std::size_t count) {
    std::uint64_t total{};
    for (std::size_t i = 0; i < count; ++i) {
        expected<void, status_code> result;
        if (samples[i].fail) {
            result = unexpected<status_code>{status_code::error};
        }
        materialize(result);
        total += result ? samples[i].value : static_cast<unsigned>(result.error());
    }
    return total;
}

CMP_KERNEL std::uint64_t cmp_scalar_return(const expected_comparison_sample *samples, std::size_t count) {
    std::uint64_t total{};
    for (std::size_t i = 0; i < count; ++i) {
        auto result = scalar_result(samples[i].value, samples[i].fail);
        total += result ? *result : static_cast<unsigned>(result.error());
    }
    return total;
}

CMP_KERNEL std::uint64_t cmp_monadic_chain(const expected_comparison_sample *samples, std::size_t count) {
    std::uint64_t total{};
    for (std::size_t i = 0; i < count; ++i) {
        auto result = scalar_result(samples[i].value, samples[i].fail)
                          .transform([](std::uint64_t value) { return value + 7; })
                          .and_then([](std::uint64_t value) -> expected<std::uint64_t, status_code> { return value * 3; });
        total += result.value_or(5);
    }
    return total;
}

CMP_KERNEL std::uint64_t cmp_scalar_assignment(const expected_comparison_sample *samples, std::size_t count) {
    expected<std::uint64_t, status_code> state{0};
    std::uint64_t                        total{};
    for (std::size_t i = 0; i < count; ++i) {
        state = scalar_result(samples[i].value, samples[i].fail);
        materialize(state);
        total += state ? *state : static_cast<unsigned>(state.error());
    }
    return total;
}

CMP_KERNEL std::uint64_t cmp_string_return(const expected_comparison_sample *samples, std::size_t count) {
    std::uint64_t total{};
    for (std::size_t i = 0; i < count; ++i) {
        auto result = string_result(samples[i].value, samples[i].fail);
        total += result ? static_cast<unsigned char>((*result)[0]) : static_cast<unsigned>(result.error());
    }
    return total;
}

CMP_KERNEL std::uint64_t cmp_string_transition(const expected_comparison_sample *samples, std::size_t count) {
    expected<std::string, status_code> state{unexpected<status_code>{status_code::error}};
    std::uint64_t                      total{};
    for (std::size_t i = 0; i < count; ++i) {
        state = string_result(samples[i].value, samples[i].fail);
        materialize(state);
        total += state ? static_cast<unsigned char>((*state)[0]) : static_cast<unsigned>(state.error());
    }
    return total;
}

CMP_KERNEL std::uint64_t cmp_checked_value(const expected_comparison_sample *samples, std::size_t count) {
    std::uint64_t total{};
    for (std::size_t i = 0; i < count; ++i) {
        auto result = scalar_result(samples[i].value, samples[i].fail);
        try {
            total += result.value();
        } catch (const bad_expected_access<status_code> &error) { total += static_cast<unsigned>(error.error()); }
    }
    return total;
}

#ifndef CMP_EXPECTED_ONLY
CMP_KERNEL std::uint64_t cmp_encode_integers(const std::vector<std::uint64_t> &values, std::vector<std::byte> &output) {
    output.clear();
    auto result = make_encoder(output)(values);
    materialize(result);
    materialize(output);
    return result ? output.size() : static_cast<unsigned>(result.error());
}

CMP_KERNEL std::uint64_t cmp_decode_integers(const std::vector<std::byte> &input, std::vector<std::uint64_t> &values) {
    values.clear();
    auto result = make_decoder(input)(values);
    materialize(result);
    materialize(values);
    return result ? values.size() : static_cast<unsigned>(result.error());
}

CMP_KERNEL std::uint64_t cmp_decode_truncated(const std::vector<std::byte> &input) {
    std::uint64_t value{};
    auto          result = make_decoder(input)(value);
    materialize(result);
    return result ? value : static_cast<unsigned>(result.error());
}

#endif

} // extern "C"
