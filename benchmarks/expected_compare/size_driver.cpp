#include "kernel_api.h"

#include <array>
#include <cbor_tags/cbor.h>

int main(int argc, char **) {
    using cbor::tags::status_code;
    constexpr auto   error_value = static_cast<unsigned>(status_code::error);
    const std::array samples{
        expected_comparison_sample{static_cast<std::uint64_t>(argc), false},
        expected_comparison_sample{24, true},
        expected_comparison_sample{65536, false},
        expected_comparison_sample{256, true},
    };
    std::uint64_t plain_sum{};
    std::uint64_t chain_sum{};
    std::uint64_t string_sum{};
    for (const auto &sample : samples) {
        plain_sum += sample.fail ? error_value : sample.value;
        chain_sum += sample.fail ? 5 : (sample.value + 7) * 3;
        string_sum += sample.fail ? error_value : 'a' + sample.value % 26;
    }
    for (auto kernel : std::array{cmp_void_materialized, cmp_scalar_return, cmp_scalar_assignment, cmp_checked_value}) {
        if (kernel(samples.data(), samples.size()) != plain_sum) {
            return 1;
        }
    }
    if (cmp_monadic_chain(samples.data(), samples.size()) != chain_sum || cmp_string_return(samples.data(), samples.size()) != string_sum ||
        cmp_string_transition(samples.data(), samples.size()) != string_sum) {
        return 2;
    }
#ifndef CMP_EXPECTED_ONLY
    const std::vector<std::uint64_t> source{1, 24, 256, 65536};
    std::vector<std::byte>           wire;
    std::vector<std::uint64_t>       decoded;
    const auto                       encoded_size = cmp_encode_integers(source, wire);
    if (wire.size() != encoded_size || wire.size() <= source.size() || cmp_decode_integers(wire, decoded) != source.size() ||
        decoded != source) {
        return 3;
    }
    const std::vector<std::byte> truncated{std::byte{0x1B}, std::byte{1}};
    return cmp_decode_truncated(truncated) != static_cast<unsigned>(status_code::incomplete);
#else
    return 0;
#endif
}
