#include "cbor_tags/cbor_decoder.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace app {

enum class decode_status {
    success,
    input_too_large,
    invalid_cbor,
};

struct decode_result {
    decode_status           status{decode_status::success};
    cbor::tags::status_code cbor_status{cbor::tags::status_code::success};

    [[nodiscard]] explicit operator bool() const noexcept { return status == decode_status::success; }
};

template <typename T> [[nodiscard]] decode_result decode_bounded(std::span<const std::byte> input, T &value, std::size_t max_input_bytes) {
    if (input.size() > max_input_bytes) {
        return {.status = decode_status::input_too_large};
    }

    auto decoder = cbor::tags::make_decoder(input);
    auto result  = decoder(value);
    if (!result) {
        return {.status = decode_status::invalid_cbor, .cbor_status = result.error()};
    }
    return {};
}

} // namespace app

int main() {
    const std::vector<std::byte> encoded{
        std::byte{0x82},
        std::byte{0x01},
        std::byte{0x02},
    };

    std::vector<std::uint64_t> decoded;
    auto                       result = app::decode_bounded(std::span{encoded}, decoded, encoded.size());
    if (!result || decoded != std::vector<std::uint64_t>{1U, 2U}) {
        return 1;
    }

    auto rejected = app::decode_bounded(std::span{encoded}, decoded, encoded.size() - 1U);
    if (rejected.status != app::decode_status::input_too_large) {
        return 2;
    }

    const std::vector<std::byte> incomplete{
        std::byte{0x82},
        std::byte{0x01},
    };
    auto invalid = app::decode_bounded(std::span{incomplete}, decoded, incomplete.size());
    if (invalid.status != app::decode_status::invalid_cbor || invalid.cbor_status != cbor::tags::status_code::incomplete) {
        return 3;
    }

    return 0;
}
