#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

namespace ct = cbor::tags;

// point has hooks of its own, and the codec only handles it in a one-shot
// encoder or decoder. That one runs the codec, a resumable one would run
// point's hook and write x, y instead
struct point {
    int x{};
    int y{};

    template <typename Encoder> auto encode(Encoder &encoder) const { return encoder(x, y); }
    template <typename Decoder> auto decode(Decoder &decoder) { return decoder(x, y); }

    template <typename Encoder> ct::resumable<ct::status_code> resumable_encode(Encoder &encoder) const { return encoder(x, y); }
    template <typename Decoder> ct::resumable<ct::status_code> resumable_decode(Decoder &decoder) { return decoder(x, y); }
};

template <typename Self> struct swapped_points : ct::codec::base<Self> {
    using ct::codec::base<Self>::decode;
    using ct::codec::base<Self>::encode;

#if defined(CBOR_TAGS_ONE_SHOT_ONLY_CODEC_MUTABLE_ENCODE)
    // Only for a point that isn't const, a one-shot encoder still picks it for one
    void encode(point &value) { static_cast<Self &>(*this)(value.y, value.x); }
#else
    void encode(const point &value) { static_cast<Self &>(*this)(value.y, value.x); }
#endif

    ct::status_code decode(point &value) {
        const auto result = static_cast<Self &>(*this)(value.y, value.x);
        return result ? ct::status_code::success : result.error();
    }
};

// Tagged, so a one-shot variant reads the tag and then asks the codec with it.
// Decoded on its own nobody asks the codec, and that still compiles
struct reading {
    static constexpr std::uint64_t cbor_tag = 100;
    int                            value{};

    template <typename Decoder> auto                           decode(Decoder &decoder) { return decoder(value); }
    template <typename Decoder> ct::resumable<ct::status_code> resumable_decode(Decoder &decoder) { return decoder(value); }
};

template <typename Self> struct offset_readings : ct::codec::base<Self> {
    using ct::codec::base<Self>::decode;
    using ct::codec::base<Self>::encode;

    ct::status_code decode(reading &value, std::uint64_t tag) {
        if (tag != reading::cbor_tag)
            return ct::status_code::no_match_for_tag;

        const auto result = static_cast<Self &>(*this)(value.value);
        if (!result)
            return result.error();

        value.value += 1000;
        return ct::status_code::success;
    }
};

int main() {
    std::vector<std::byte> bytes;
    point                  value{1, 2};

    // Not one of the cases, this has to keep compiling in all of them
    reading alone;
    auto    plain_decoder = ct::make_resumable_decoder<offset_readings>(bytes);
    auto    plain         = plain_decoder(alone);
    (void)plain;

#if defined(CBOR_TAGS_ONE_SHOT_ONLY_CODEC_ENCODE) || defined(CBOR_TAGS_ONE_SHOT_ONLY_CODEC_MUTABLE_ENCODE)
    auto encoder   = ct::make_resumable_encoder<swapped_points>(bytes);
    auto operation = encoder(value);
#elif defined(CBOR_TAGS_ONE_SHOT_ONLY_CODEC_DECODE)
    auto decoder   = ct::make_resumable_decoder<swapped_points>(bytes);
    auto operation = decoder(value);
#elif defined(CBOR_TAGS_ONE_SHOT_ONLY_CODEC_VARIANT_DECODE)
    std::variant<reading, int> either;
    auto                       decoder   = ct::make_resumable_decoder<offset_readings>(bytes);
    auto                       operation = decoder(either);
#endif
    (void)operation;
}
