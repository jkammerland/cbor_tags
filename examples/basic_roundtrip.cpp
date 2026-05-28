#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct sample_record {
    static constexpr std::uint64_t cbor_tag = 1001;
    std::uint32_t                  id{};
    std::string                    label;
};

int main() {
    using namespace std::string_view_literals;

    std::vector<std::byte> buffer;
    auto                   enc   = cbor::tags::make_encoder(buffer);
    const sample_record    input = {.id = 42, .label = "ready"};
    if (!enc(input)) {
        return 1;
    }

    sample_record output;
    auto          dec = cbor::tags::make_decoder(buffer);
    if (!dec(output)) {
        return 2;
    }

    return output.id == input.id && output.label == "ready"sv ? 0 : 3;
}
