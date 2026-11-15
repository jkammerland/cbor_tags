#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ct = cbor::tags;

// No hooks. A resumable encoder or decoder goes through point field by field
// and never asks a one-shot codec about it
struct point {
    int x{};
    int y{};
};

// Tagged, so a one-shot variant reads the tag and then asks the codecs with it
struct reading {
    static constexpr std::uint64_t cbor_tag = 100;
    int                            value{};
};

// None of the mixins below derive from the codec base. A one-shot encoder or
// decoder calls them all the same. Where it would, and the resumable side
// handles the value itself without asking, the two would disagree. Those are
// the cases at the end of main, the rest of main has to keep compiling

// y before x
template <typename Self> struct swapped_points_encoder {
#if defined(CBOR_TAGS_PLAIN_CODEC_MUTABLE_ENCODE)
    // Only for a point that isn't const, a one-shot encoder still picks it for one
    void encode(point &value) { static_cast<Self &>(*this).encode(ct::wrap_as_array{value.y, value.x}); }
#else
    void encode(const point &value) { static_cast<Self &>(*this).encode(ct::wrap_as_array{value.y, value.x}); }
#endif
};

// Back to front
template <typename Self> struct reversed_ints_encoder {
    void encode(const std::vector<int> &value) {
        auto &self = static_cast<Self &>(*this);

        self.encode(ct::as_array{value.size()});
        for (auto i = value.size(); i-- > 0;)
            self.encode(value[i]);
    }
};

// Takes a view, and a std::string only gets here through a conversion. A
// one-shot encoder writes the string itself then, so nobody disagrees
template <typename Self> struct shouting_encoder {
    void encode(std::string_view value) { static_cast<Self &>(*this).encode(std::string(value) + "!"); }
};

// For a number. Those are written in one piece, the resumable encoder asks
// the one-shot one for them and this gets called from there
template <typename Self> struct offset_uint32_encoder {
    void encode(std::uint32_t value) { static_cast<Self &>(*this).encode(std::uint64_t{value} + 1000); }
};

// A one-shot decoder calls this for a point it starts on. Right inside an
// optional it has read the header already and can't
template <typename Self> struct swapped_points_decoder {
    ct::status_code decode(point &value) { return static_cast<Self &>(*this).decode(ct::wrap_as_array{value.y, value.x}); }
};

// The same with the header read, so this is the one for inside an optional
template <typename Self> struct swapped_points_header_decoder {
    ct::status_code decode(point &value, ct::major_type major, std::byte info) {
        auto &self = static_cast<Self &>(*this);

        ct::as_array_any header;
        auto             status = self.decode(header, major, info);
        if (status != ct::status_code::success)
            return status;
        if (header.size != 2)
            return ct::status_code::unexpected_group_size;

        status = self.decode(value.y);
        return status == ct::status_code::success ? self.decode(value.x) : status;
    }
};

// Back to front, for a vector a one-shot decoder starts on
template <typename Self> struct reversed_ints_decoder {
    ct::status_code decode(std::vector<int> &value) {
        auto &self = static_cast<Self &>(*this);

        ct::as_array_any header;
        auto             status = self.decode(header);
        if (status != ct::status_code::success)
            return status;

        value.assign(header.size, 0);
        for (auto i = value.size(); i-- > 0 && status == ct::status_code::success;)
            status = self.decode(value[i]);
        return status;
    }
};

// A one-shot variant calls this once it has read the tag
template <typename Self> struct offset_readings_decoder {
    ct::status_code decode(reading &value, std::uint64_t tag) {
        if (tag != reading::cbor_tag)
            return ct::status_code::no_match_for_tag;

        const auto status = static_cast<Self &>(*this).decode(value.value);
        if (status == ct::status_code::success)
            value.value += 1000;
        return status;
    }
};

// The same for reading a number
template <typename Self> struct offset_uint32_decoder {
    ct::status_code decode(std::uint32_t &value, ct::major_type major, std::byte info) {
        std::uint64_t wire_value{};
        const auto    status = static_cast<Self &>(*this).decode(wire_value, major, info);
        if (status == ct::status_code::success)
            value = static_cast<std::uint32_t>(wire_value + 1000);
        return status;
    }
};

// This one has the codec base, and none of the above is its business. Its
// return type is deduced, so asking it about a std::string directly is an
// error inside its body. A one-shot encoder never gets that far, its own
// encode for strings fits better
template <typename Self> struct to_cbor_encoder : ct::codec::encoder_base<Self> {
    using ct::codec::encoder_base<Self>::encode;

    template <typename T> auto encode(const T &value) { return static_cast<Self &>(*this).encode(value.to_cbor()); }
};

// Has a hook of its own and no to_cbor() either
struct named {
    std::string name;

    template <typename Encoder> ct::resumable<ct::status_code> resumable_encode(Encoder &encoder) const { return encoder(name); }
};

template <typename Self> struct cbor::tags::resumable_retry_traits<std::uint32_t, offset_uint32_encoder<Self>> {
    static constexpr bool encode = true;
    static constexpr bool decode = false;
};

template <typename Self> struct cbor::tags::resumable_retry_traits<std::uint32_t, offset_uint32_decoder<Self>> {
    static constexpr bool encode = false;
    static constexpr bool decode = true;
};

namespace {

std::vector<std::byte> bytes_of(std::initializer_list<int> values) {
    std::vector<std::byte> bytes;
    for (const int value : values)
        bytes.push_back(static_cast<std::byte>(value));
    return bytes;
}

// The codec only takes a view, both encoders write the string as it is
bool string_keeps_its_bytes() {
    const std::string      text{"hi"};
    std::vector<std::byte> one_shot_bytes;
    std::vector<std::byte> resumable_bytes;

    const auto result    = ct::make_encoder<shouting_encoder>(one_shot_bytes)(text);
    auto       encoder   = ct::make_resumable_encoder<shouting_encoder>(resumable_bytes);
    auto       operation = encoder(text);

    const auto expected = bytes_of({0x62, 'h', 'i'});
    return result && operation.done() && operation.result() && one_shot_bytes == expected && resumable_bytes == expected;
}

// A codec with the base is left alone, for a string and for a type with a hook
bool codec_with_base_is_not_asked() {
    const std::string      text{"hi"};
    const named            value{"hi"};
    std::vector<std::byte> text_bytes;
    std::vector<std::byte> value_bytes;

    auto text_encoder    = ct::make_resumable_encoder<to_cbor_encoder>(text_bytes);
    auto text_operation  = text_encoder(text);
    auto value_encoder   = ct::make_resumable_encoder<to_cbor_encoder>(value_bytes);
    auto value_operation = value_encoder(value);

    const auto expected = bytes_of({0x62, 'h', 'i'});
    const bool text_ok  = text_operation.done() && text_operation.result() && text_bytes == expected;
    const bool value_ok = value_operation.done() && value_operation.result() && value_bytes == expected;
    return text_ok && value_ok;
}

// 7, both encoders get to the codec for the number
bool number_encoder_is_called() {
    const std::uint32_t    value{7};
    std::vector<std::byte> one_shot_bytes;
    std::vector<std::byte> resumable_bytes;

    const auto result    = ct::make_encoder<offset_uint32_encoder>(one_shot_bytes)(value);
    auto       encoder   = ct::make_resumable_encoder<offset_uint32_encoder>(resumable_bytes);
    auto       operation = encoder(value);

    const auto expected = bytes_of({0x19, 0x03, 0xef});
    return result && operation.done() && operation.result() && one_shot_bytes == expected && resumable_bytes == expected;
}

// [1, 2] right inside an optional. Neither decoder can call a codec that has
// to start on the point, so both read x, y
bool point_in_optional_reads_in_order() {
    const auto           bytes = bytes_of({0x82, 0x01, 0x02});
    std::optional<point> one_shot_value;
    std::optional<point> resumable_value;

    const auto result    = ct::make_decoder<swapped_points_decoder>(bytes)(one_shot_value);
    auto       decoder   = ct::make_resumable_decoder<swapped_points_decoder>(bytes);
    auto       operation = decoder(resumable_value);

    const auto in_order = [](const std::optional<point> &value) { return value && value->x == 1 && value->y == 2; };
    return result && operation.done() && operation.result() && in_order(one_shot_value) && in_order(resumable_value);
}

// The same for [1, 2, 3] and the codec that reads a vector back to front
bool ints_in_optional_read_in_order() {
    const auto                      bytes = bytes_of({0x83, 0x01, 0x02, 0x03});
    std::optional<std::vector<int>> one_shot_value;
    std::optional<std::vector<int>> resumable_value;

    const auto result    = ct::make_decoder<reversed_ints_decoder>(bytes)(one_shot_value);
    auto       decoder   = ct::make_resumable_decoder<reversed_ints_decoder>(bytes);
    auto       operation = decoder(resumable_value);

    const std::optional<std::vector<int>> expected{{1, 2, 3}};
    return result && operation.done() && operation.result() && one_shot_value == expected && resumable_value == expected;
}

// 7 right inside an optional, both decoders get to the codec for the number.
// And it leaves a point alone
bool number_decoder_is_called() {
    const auto                   bytes = bytes_of({0x07});
    std::optional<std::uint32_t> one_shot_value;
    std::optional<std::uint32_t> resumable_value;

    const auto result    = ct::make_decoder<offset_uint32_decoder>(bytes)(one_shot_value);
    auto       decoder   = ct::make_resumable_decoder<offset_uint32_decoder>(bytes);
    auto       operation = decoder(resumable_value);

    const auto point_bytes = bytes_of({0x82, 0x01, 0x02});
    point      value;
    auto       point_decoder   = ct::make_resumable_decoder<offset_uint32_decoder>(point_bytes);
    auto       point_operation = point_decoder(value);

    const bool numbers_ok = result && operation.done() && operation.result() && one_shot_value == 1007U && resumable_value == 1007U;
    const bool point_ok   = point_operation.done() && point_operation.result() && value.x == 1 && value.y == 2;
    return numbers_ok && point_ok;
}

} // namespace

int main() {
    const bool ok = string_keeps_its_bytes() && codec_with_base_is_not_asked() && number_encoder_is_called() &&
                    point_in_optional_reads_in_order() && ints_in_optional_read_in_order() && number_decoder_is_called();

    [[maybe_unused]] std::vector<std::byte> bytes;

#if defined(CBOR_TAGS_PLAIN_CODEC_ENCODE) || defined(CBOR_TAGS_PLAIN_CODEC_MUTABLE_ENCODE)
    point value{1, 2};
    auto  encoder   = ct::make_resumable_encoder<swapped_points_encoder>(bytes);
    auto  operation = encoder(value);
#elif defined(CBOR_TAGS_PLAIN_CODEC_CONTAINER_ENCODE)
    const std::vector<int> value{1, 2, 3};
    auto                   encoder   = ct::make_resumable_encoder<reversed_ints_encoder>(bytes);
    auto                   operation = encoder(value);
#elif defined(CBOR_TAGS_PLAIN_CODEC_DECODE)
    point value;
    auto  decoder   = ct::make_resumable_decoder<swapped_points_decoder>(bytes);
    auto  operation = decoder(value);
#elif defined(CBOR_TAGS_PLAIN_CODEC_CONTAINER_DECODE)
    std::vector<int> value;
    auto             decoder   = ct::make_resumable_decoder<reversed_ints_decoder>(bytes);
    auto             operation = decoder(value);
#elif defined(CBOR_TAGS_PLAIN_CODEC_HEADER_DECODE)
    std::optional<point> value;
    auto                 decoder   = ct::make_resumable_decoder<swapped_points_header_decoder>(bytes);
    auto                 operation = decoder(value);
#elif defined(CBOR_TAGS_PLAIN_CODEC_TAG_DECODE)
    std::variant<reading, int> value;
    auto                       decoder   = ct::make_resumable_decoder<offset_readings_decoder>(bytes);
    auto                       operation = decoder(value);
#endif

    return ok ? 0 : 1;
}
