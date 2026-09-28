#pragma once

#include "fuzz_support.h"
#include "wire_vocabulary.h"

#include <tuple>

namespace cbor_fuzz {
inline auto wire_domain() {
    return fuzztest::VectorOf(fuzztest::Arbitrary<std::uint8_t>()).WithMaxSize(4096).WithDictionary(wire_vocabulary);
}

inline std::vector<std::tuple<bytes>> wire_seeds() {
    std::vector<std::tuple<bytes>> seeds{{bytes{}}};
    for (auto token : wire_vocabulary()) {
        seeds.emplace_back(std::move(token));
    }
    return seeds;
}
} // namespace cbor_fuzz
