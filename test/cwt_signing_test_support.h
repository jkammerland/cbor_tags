#pragma once

#include "cwt_test_support.h"

namespace cwt_test {

struct toy_es256_backend {
    static constexpr algorithm                algorithm_id = algorithm::es256;
    static expected<byte_string, status_code> sign(void *, std::span<const std::byte>) { return byte_string{}; }
    static expected<void, status_code>        verify(void *, std::span<const std::byte>, std::span<const std::byte>) { return {}; }
};

struct signature_presence_es256_backend {
    static constexpr algorithm algorithm_id = algorithm::es256;

    static expected<byte_string, status_code> sign(void *, std::span<const std::byte>) { return byte_string{std::byte{0x01}}; }

    static expected<void, status_code> verify(void *, std::span<const std::byte>, std::span<const std::byte> signature) {
        if (signature.empty()) {
            return cbor::tags::unexpected<status_code>{status_code::error};
        }
        return {};
    }
};

} // namespace cwt_test
