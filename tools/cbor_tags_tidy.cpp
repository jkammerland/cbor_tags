#include <array>
#include <cbor_tags/cbor.h>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_traversal.h>
#include <cbor_tags/extensions/cbor_visualization.h>
#include <cstddef>
#include <deque>

[[maybe_unused]] constexpr int cbor_tags_tidy_anchor() { return 0; }

// Instantiate both callback return forms and both borrowed payload shapes.
[[maybe_unused]] void cbor_tags_traversal_tidy_anchor() {
    namespace ct = cbor::tags;
    const std::array<std::byte, 1>                         contiguous{};
    ct::decoder<decltype(contiguous), ct::default_options> dec{contiguous};
    (void)ct::walk_item(dec, [](const auto &, const auto &) {});
    ct::decoder<decltype(contiguous), ct::default_options> validator{contiguous};
    (void)ct::validate_item(validator);

    const std::deque<std::byte>                               noncontiguous{};
    ct::decoder<decltype(noncontiguous), ct::default_options> range_dec{noncontiguous};
    (void)ct::walk_item(range_dec, [](const auto &, const auto &) { return ct::status_code::success; }, {.strict_validation = true});
}
