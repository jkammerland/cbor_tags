#include "kernel_api.h"

#include <array>
#include <cbor_tags/cbor.h>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using namespace cbor::tags;
constexpr auto error_value = static_cast<unsigned>(status_code::error);

void require(bool success) {
    if (!success) {
        std::cerr << "comparison fixture failed\n";
        std::abort();
    }
}

template <typename F> void measure(std::string_view name, std::size_t batch, F operation) {
    using clock = std::chrono::steady_clock;
    for (int i = 0; i < 100; ++i) {
        const auto value = operation();
        asm volatile("" : : "g"(value) : "memory");
    }
    auto        start = clock::now();
    auto        end   = start;
    std::size_t count{};
    do {
        for (int i = 0; i < 64; ++i) {
            const auto value = operation();
            asm volatile("" : : "g"(value) : "memory");
        }
        count += 64;
        end = clock::now();
    } while (end - start < std::chrono::milliseconds{40});
    std::cout << name << ',' << std::chrono::duration<double, std::nano>(end - start).count() / (count * batch) << '\n';
}

std::uint64_t reference(const std::array<expected_comparison_sample, 64> &samples, int operation) {
    std::uint64_t sum{};
    for (const auto &sample : samples) {
        if (operation == 1) {
            sum += sample.fail ? 5 : (sample.value + 7) * 3;
        } else if (operation == 2) {
            sum += sample.fail ? error_value : 'a' + sample.value % 26;
        } else {
            sum += sample.fail ? error_value : sample.value;
        }
    }
    return sum;
}

} // namespace

int main() {
    const std::array                  kernels{cmp_void_materialized, cmp_scalar_return,     cmp_monadic_chain, cmp_scalar_assignment,
                                              cmp_string_return,     cmp_string_transition, cmp_checked_value};
    const std::array<const char *, 7> names{"void_materialized", "scalar_return",     "monadic_chain", "scalar_assignment",
                                            "string_return",     "string_transition", "checked_value"};
    std::cout << "case,ns_per_item\n" << std::setprecision(10);
    for (int pattern : {0, 1, 2}) {
        std::array<expected_comparison_sample, 64> samples{};
        for (std::size_t i = 0; i < samples.size(); ++i) {
            samples[i] = {(i * 7919 + 31) % 100000, pattern == 1 ? i % 2 == 0 : pattern == 2 && i == 31};
        }
        for (std::size_t i = 0; i < kernels.size(); ++i) {
            const auto expected_sum = reference(samples, i == 2 ? 1 : i == 4 || i == 5 ? 2 : 0);
            require(kernels[i](samples.data(), samples.size()) == expected_sum);
            std::string name = std::string(names[i]) + (pattern == 0 ? "/success" : pattern == 1 ? "/half_error" : "/one_error_in_64");
            measure(name, samples.size(), [&] { return kernels[i](samples.data(), samples.size()); });
        }
    }
    for (std::size_t size : {32, 4096}) {
        std::vector<std::uint64_t> source(size);
        for (std::size_t i = 0; i < size; ++i) {
            source[i] = (i * 7919) % 100000;
        }
        std::vector<std::byte> wire;
        const auto             wire_size = cmp_encode_integers(source, wire);
        require(wire_size == wire.size() && wire_size > source.size());
        std::vector<std::uint64_t> decoded;
        require(cmp_decode_integers(wire, decoded) == source.size());
        require(decoded == source);
        std::vector<std::byte> output;
        output.reserve(wire.size());
        decoded.reserve(source.size());
        measure("codec_encode/uints" + std::to_string(size), 1, [&] { return cmp_encode_integers(source, output); });
        require(output == wire);
        measure("codec_decode/uints" + std::to_string(size), 1, [&] { return cmp_decode_integers(wire, decoded); });
        require(decoded == source);
    }
    const std::vector<std::byte> truncated{std::byte{0x1B}, std::byte{1}};
    require(cmp_decode_truncated(truncated) == static_cast<unsigned>(status_code::incomplete));
    measure("codec_decode/truncated_uint", 1, [&] { return cmp_decode_truncated(truncated); });
}
