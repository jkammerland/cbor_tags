#pragma once

#include "cwt_test_support.h"

namespace cwt_test {

struct toy_es256_backend {
    static constexpr algorithm algorithm_id = algorithm::es256;

    template <std::ranges::input_range Parts> static expected<std::vector<std::byte>, status_code> sign(void *, Parts &&) {
        return std::vector<std::byte>{};
    }
    template <std::ranges::input_range Parts> static expected<void, status_code> verify(void *, Parts &&, std::span<const std::byte>) {
        return {};
    }
};

struct signature_presence_es256_backend {
    static constexpr algorithm algorithm_id = algorithm::es256;

    template <std::ranges::input_range Parts> static expected<std::vector<std::byte>, status_code> sign(void *, Parts &&) {
        return std::vector<std::byte>{std::byte{0x01}};
    }

    template <std::ranges::input_range Parts>
    static expected<void, status_code> verify(void *, Parts &&, std::span<const std::byte> signature) {
        if (signature.empty()) {
            return cbor::tags::unexpected<status_code>{status_code::error};
        }
        return {};
    }
};

} // namespace cwt_test
