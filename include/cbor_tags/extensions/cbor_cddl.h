#include <cbor_tags/cbor_concepts.h>
#include <fmt/format.h>
#include <iterator>

namespace cbor::tags {

struct FormattingOptions {
    bool pretty_print{false};
};

template <typename CborBuffer, typename OutputBuffer> constexpr auto CDDL(const CborBuffer &cbor_buffer, OutputBuffer &output_buffer) {
    for (const auto &byte : cbor_buffer) {
        fmt::format_to(std::back_inserter(output_buffer), "{:02X}", static_cast<std::uint8_t>(byte));
    }
}

} // namespace cbor::tags