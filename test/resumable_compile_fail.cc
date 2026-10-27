#include <array>
#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <tuple>
#include <vector>

using namespace cbor::tags;

struct empty_item {};
struct custom_value {
    int value{};
};

template <typename Self> struct synchronous_codec {
    void        encode(const custom_value &) {}
    status_code decode(custom_value &, major_type, std::byte) { return status_code::success; }
};

int main() {
#if defined(UNSAFE_INPUT)
    int  value{};
    auto operation = make_resumable_decoder()(value);
    (void)operation(std::vector<std::byte>{std::byte{1}});
#elif defined(UNSAFE_BATCH)
    int  value{};
    auto operation = make_resumable_decoder()(value);
    (void)operation(std::vector<std::vector<std::byte>>{{std::byte{1}}});
#elif defined(UNSAFE_TUPLE)
    auto operation = make_resumable_decoder()(std::tuple<int>{});
#elif defined(UNSAFE_ARRAY_WRAPPER)
    auto operation = make_resumable_decoder()(wrap_as_array{1, 2});
#elif defined(EOF_NOT_LAST)
    int                      value{};
    std::array<std::byte, 1> bytes{};
    auto                     operation = make_resumable_decoder()(value);
    (void)operation(end_of_input, bytes);
#elif defined(ZERO_WIDTH_GENERATOR)
    auto operation = make_resumable_decoder().items<empty_item>();
#elif defined(HEADER_GENERATOR)
    auto operation = make_resumable_decoder().items<as_array_any>();
#elif defined(ZERO_WIDTH_CONTAINER)
    std::vector<empty_item> values;
    auto                    operation = make_resumable_decoder()(values);
#elif defined(SYNC_ENCODER_CODEC)
    auto operation = make_resumable_encoder<synchronous_codec>()(custom_value{});
#elif defined(SYNC_DECODER_CODEC)
    custom_value value;
    auto         operation = make_resumable_decoder<synchronous_codec>()(value);
#endif
}
