#include <cbor_tags/cbor_encoder.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

static std::string hex(const std::vector<std::byte> &bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string    result;
    result.reserve(bytes.size() * 2U);
    for (std::byte byte : bytes) {
        const auto value = std::to_integer<unsigned>(byte);
        result.push_back(digits[value >> 4U]);
        result.push_back(digits[value & 0x0FU]);
    }
    return result;
}

int main() {
    std::vector<std::byte> buffer;
    auto                   enc = cbor::tags::make_encoder(buffer);

    std::array<std::tuple<int, int, int>, 1> entries{std::tuple{1, 2, 3}};
    auto                                     result = enc(cbor::tags::as_map_range(entries));
    if (!result) {
        std::puts("encoding failed unexpectedly");
        return 2;
    }

    const auto encoded = hex(buffer);
    if (encoded != "a10102") {
        std::printf("unexpected encoding: %s\n", encoded.c_str());
        return 1;
    }

    std::puts("PROOF: as_map_range(tuple<int,int,int>) encoded a10102 and dropped the third field");
    return 0;
}
