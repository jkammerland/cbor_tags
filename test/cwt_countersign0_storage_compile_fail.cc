#include <cbor_tags/cwt/countersignatures.h>
#include <cstddef>
#include <ranges>
#include <span>
#include <vector>

using namespace cbor::tags;

namespace {

struct backend {
    static constexpr cwt::algorithm algorithm_id = cwt::algorithm::es256;

    template <std::ranges::input_range Parts> static expected<std::vector<std::byte>, status_code> sign(void *, Parts &&) {
        return std::vector<std::byte>{};
    }

    template <std::ranges::input_range Parts> static expected<std::size_t, status_code> sign(void *, Parts &&, std::span<std::byte>) {
        return std::size_t{0};
    }
};

} // namespace

int main() {
    cwt::cose_sign1        target{};
    std::vector<std::byte> aad{std::byte{0x03}};
    std::vector<std::byte> payload{std::byte{0x01}};

    // Storage and AAD, or AAD and detached payload? A writable container in
    // the third place has to be named as one or the other
    auto result = cwt::countersign0<backend>(nullptr, target, aad, payload);
    return result ? 0 : 1;
}
