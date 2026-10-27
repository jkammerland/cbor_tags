#include <array>
#include <cbor_tags/cbor.h>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cbor_tags/cbor_traversal.h>
#include <cbor_tags/extensions/cbor_visualization.h>
#include <cstddef>
#include <deque>
#include <string>
#include <vector>

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

namespace {

struct resumable_tidy_value {
    int                              value{};
    template <typename Context> auto decode(Context &context) { return context(value); }
    template <typename Context> auto encode(Context &context) const { return context(value); }
};

} // namespace

// Instantiate typed and generator paths with non-contiguous caller buffers.
[[maybe_unused]] void cbor_tags_resumable_tidy_anchor() {
    namespace ct = cbor::tags;
    std::deque<std::byte> input;
    std::deque<std::byte> output(64);
    int                   number{};
    std::string           text;
    std::vector<int>      values;
    resumable_tidy_value  custom;
    auto                  decoder = ct::make_resumable_decoder();
    auto                  decode  = decoder(number, text, values, custom);
    (void)decode(input);
    auto items = decoder.items<int>();
    (void)items(input);
    auto payload = decoder.bstr();
    (void)payload(input);
    auto validator = ct::make_resumable_validator();
    (void)validator(input);
    auto encoder = ct::make_resumable_encoder();
    auto encode  = encoder(number, text, values, custom);
    (void)encode(output);
    auto chunks = encoder.chunks(values);
    (void)chunks(output);
    auto next = chunks.next();
    if (next.await_ready())
        (void)next.await_resume();
}
