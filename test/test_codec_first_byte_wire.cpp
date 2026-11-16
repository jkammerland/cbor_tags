#include "test_util.h"

#include <algorithm>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cbor_tags/codec.h>
#include <cbor_tags/codec/shared_ptr.h>
#include <cbor_tags/codec/unique_ptr.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <version>

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
#include <cbor_tags/codec/std_expected.h>
#endif

#if defined(__cpp_lib_indirect) && __cpp_lib_indirect >= 202502L
#include <cbor_tags/codec/std_indirect.h>
#endif

using namespace cbor::tags;

namespace {

// A peer sends positions as [y, x], the struct has x first
struct position {
    int x{};
    int y{};

    friend bool operator==(const position &, const position &) = default;

    friend std::ostream &operator<<(std::ostream &out, const position &value) { return out << "{x=" << value.x << " y=" << value.y << "}"; }
};

// One field, so a decoder that has read a header hands it on to that field
struct holder {
    position where;

    friend bool operator==(const holder &, const holder &) = default;

    friend std::ostream &operator<<(std::ostream &out, const holder &value) { return out << "{where=" << value.where << "}"; }
};

// Tagged, a variant reads the tag before it knows which alternative it is
template <std::uint64_t Tag> struct reading {
    static constexpr std::uint64_t cbor_tag = Tag;
    int                            value{};

    friend bool operator==(const reading &, const reading &) = default;

    friend std::ostream &operator<<(std::ostream &out, const reading &value) { return out << "{value=" << value.value << "}"; }
};

// An encoder writes an enum as the number it is made of, through the codecs
// for that number. Reading one has to go through them as well
enum class level : std::uint32_t { low = 1007, high = 2000 };

// These have no reflected payload; only their codec supplies a complete item.
struct marker {
    bool operator==(const marker &) const = default;
};

struct tagged_marker {
    static constexpr std::uint64_t cbor_tag = 100;

    bool operator==(const tagged_marker &) const = default;
};

template <typename Self> struct markers : codec::decoder_base<Self> {
    using codec::decoder_base<Self>::decode;

    status_code decode(marker &) {
        unsigned   value{};
        const auto status = static_cast<Self &>(*this).decode(value);
        if (status != status_code::success)
            return status;
        return value == 7U ? status_code::success : status_code::error;
    }

    status_code decode(tagged_marker &) {
        auto      &self   = static_cast<Self &>(*this);
        const auto status = self.decode(static_tag<100>{});
        if (status != status_code::success)
            return status;
        marker payload;
        return decode(payload);
    }
};

// The codecs down to marked_numbers only have decode(T &), the form for a
// decoder that stands on the first byte of the value

// A sensor sends whole centi-degrees, 2150 is 21.50
template <typename Self> struct centi_degrees : codec::decoder_base<Self> {
    using codec::decoder_base<Self>::decode;

    status_code decode(float &degrees) {
        std::int64_t centi{};
        const auto   status = static_cast<Self &>(*this).decode(centi);
        if (status == status_code::success)
            degrees = static_cast<float>(centi) / 100.0F;
        return status;
    }
};

// y comes first on the wire
template <typename Self> struct y_first : codec::decoder_base<Self> {
    using codec::decoder_base<Self>::decode;

    status_code decode(position &value) { return static_cast<Self &>(*this).decode(wrap_as_array{value.y, value.x}); }
};

// 1000 more than on the wire, so the value says that the codec ran
template <typename Self> struct offset_numbers : codec::decoder_base<Self> {
    using codec::decoder_base<Self>::decode;

    status_code decode(std::uint32_t &value) {
        std::uint64_t wire_value{};
        const auto    status = static_cast<Self &>(*this).decode(wire_value);
        if (status == status_code::success)
            value = static_cast<std::uint32_t>(wire_value + 1000);
        return status;
    }
};

// Reads the tag itself, and 1000 more than on the wire again
template <typename Self> struct offset_readings : codec::decoder_base<Self> {
    using codec::decoder_base<Self>::decode;

    template <std::uint64_t Tag> status_code decode(reading<Tag> &value) {
        auto &self = static_cast<Self &>(*this);

        as_tag_any tag;
        auto       status = self.decode(tag);
        if (status != status_code::success)
            return status;
        if (tag.tag != Tag)
            return status_code::no_match_for_tag;

        status = self.decode(value.value);
        if (status == status_code::success)
            value.value += 1000;
        return status;
    }
};

// Both forms: 1000 more from the first byte, 2000 more with the header read
template <typename Self> struct marked_numbers : codec::decoder_base<Self> {
    using codec::decoder_base<Self>::decode;

    status_code decode(std::uint32_t &value) {
        std::uint64_t wire_value{};
        const auto    status = static_cast<Self &>(*this).decode(wire_value);
        if (status == status_code::success)
            value = static_cast<std::uint32_t>(wire_value + 1000);
        return status;
    }

    status_code decode(std::uint32_t &value, major_type major, std::byte additional_info) {
        std::uint64_t wire_value{};
        const auto    status = static_cast<Self &>(*this).decode(wire_value, major, additional_info);
        if (status == status_code::success)
            value = static_cast<std::uint32_t>(wire_value + 2000);
        return status;
    }
};

} // namespace

// All of them only write the value once they have it, so the resumable
// decoder may run them again from the start
namespace cbor::tags {

template <typename Self> struct resumable_retry_traits<marker, markers<Self>> {
    static constexpr bool decode = true;
};
template <typename Self> struct resumable_retry_traits<tagged_marker, markers<Self>> {
    static constexpr bool decode = true;
};

template <typename Self> struct resumable_retry_traits<float, centi_degrees<Self>> {
    static constexpr bool decode = true;
};
template <typename Self> struct resumable_retry_traits<position, y_first<Self>> {
    static constexpr bool decode = true;
};
template <typename Self> struct resumable_retry_traits<std::uint32_t, offset_numbers<Self>> {
    static constexpr bool decode = true;
};
template <std::uint64_t Tag, typename Self> struct resumable_retry_traits<reading<Tag>, offset_readings<Self>> {
    static constexpr bool decode = true;
};
template <typename Self> struct resumable_retry_traits<std::uint32_t, marked_numbers<Self>> {
    static constexpr bool decode = true;
};

// A unique pointer gets a new pointee on every run
template <typename T, typename Self> struct resumable_retry_traits<std::unique_ptr<T>, codec::unique_ptr<Self>> {
    static constexpr bool decode = true;
};

} // namespace cbor::tags

namespace {

using input_chunks = std::list<std::span<const std::byte>>;

// Every wire below is followed by 42. A decoder that ends up in the wrong
// place after the value reads something else there
constexpr std::string_view TRAILER_HEX{"182a"};
constexpr int              TRAILER{42};

// One T and the trailer, with a one-shot decoder over the whole input
template <typename T, typename Input, template <typename> typename... Codecs> T read_one_shot(const Input &input) {
    T   value{};
    int trailer{};

    auto decoder = make_decoder<Codecs...>(input);
    auto result  = decoder(value, trailer);

    REQUIRE(result);
    CHECK(trailer == TRAILER);
    return value;
}

// The same with a resumable decoder that gets the input in pieces of this size
template <typename T, template <typename> typename... Codecs> T read_resumable(const std::vector<std::byte> &bytes, std::size_t piece) {
    const std::span<const std::byte> input{bytes};
    input_chunks                     storage;
    auto                             source = make_input_source(storage);

    T   value{};
    int trailer{};

    auto decoder   = make_resumable_decoder<Codecs...>(source);
    auto operation = decoder(value, trailer);

    for (std::size_t offset = 0; offset < input.size() && !operation.done(); offset += piece) {
        storage.emplace_back(input.subspan(offset, std::min(piece, input.size() - offset)));
        operation.resume();
    }

    REQUIRE(operation.done());
    REQUIRE(operation.result());
    CHECK(*operation.result() == bytes.size());
    CHECK(trailer == TRAILER);
    return value;
}

// The bytes through a one-shot decoder, from a vector and from a deque. Each
// value that comes back is handed to look
template <typename T, template <typename> typename... Codecs> void check_one_shot_reads(std::string_view hex, const auto &look) {
    CAPTURE(hex);
    const auto                  bytes = to_bytes(std::string(hex) + std::string(TRAILER_HEX));
    const std::deque<std::byte> segments(bytes.begin(), bytes.end());

    {
        INFO("one-shot");
        look(read_one_shot<T, std::vector<std::byte>, Codecs...>(bytes));
    }
    {
        INFO("one-shot, input that isn't contiguous");
        look(read_one_shot<T, std::deque<std::byte>, Codecs...>(segments));
    }
}

// And through a resumable decoder as well, in one piece and a byte at a time
template <typename T, template <typename> typename... Codecs> void check_reads_as(std::string_view hex, const auto &look) {
    check_one_shot_reads<T, Codecs...>(hex, look);

    CAPTURE(hex);
    const auto bytes = to_bytes(std::string(hex) + std::string(TRAILER_HEX));

    {
        INFO("resumable");
        look(read_resumable<T, Codecs...>(bytes, bytes.size()));
    }
    {
        INFO("resumable, a byte at a time");
        look(read_resumable<T, Codecs...>(bytes, 1));
    }
}

// All four have to come out as the value written here
template <typename T, template <typename> typename... Codecs> void check_reads(std::string_view hex, const T &expected) {
    check_reads_as<T, Codecs...>(hex, [&](const T &value) { CHECK(value == expected); });
}

// For a pointer: it has to come back holding this
template <typename Pointer> auto points_at(typename Pointer::element_type expected) {
    return [expected](const Pointer &value) {
        REQUIRE(value);
        CHECK(*value == expected);
    };
}

} // namespace

TEST_SUITE("cbor_wire/codec_first_byte") {
    TEST_CASE("a first-byte codec supplies the complete item after a header or tag") {
        using decoder_type = decltype(make_decoder<markers>(std::declval<const std::vector<std::byte> &>()));
        using core_type    = decltype(make_decoder(std::declval<const std::vector<std::byte> &>()));
        using context      = detail::payload_decode_context;

        CHECK(detail::codec_payload_encodes_one_item<decoder_type, marker>(detail::payload_context<context::header>{}));
        CHECK(detail::codec_payload_encodes_one_item<decoder_type, tagged_marker>(detail::payload_context<context::tag>{}));
        CHECK(detail::codec_payload_encodes_one_item<decoder_type, std::optional<marker>>());
        CHECK(detail::codec_payload_encodes_one_item<decoder_type, std::vector<marker>>());
        CHECK_FALSE(detail::codec_payload_encodes_one_item<core_type, marker>(detail::payload_context<context::header>{}));
        CHECK_FALSE(detail::codec_payload_encodes_one_item<core_type, tagged_marker>(detail::payload_context<context::tag>{}));

        check_reads<std::optional<marker>, markers>("07", marker{});
        check_reads<std::vector<marker>, markers>("9f0707ff", {marker{}, marker{}});
        check_reads<std::variant<tagged_marker, int>, markers>("d86407", tagged_marker{});
    }

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
    TEST_CASE("std expected accepts nested payloads supplied by a first-byte codec") {
        using optional_value = std::expected<std::optional<marker>, int>;
        using array_value    = std::expected<std::vector<marker>, int>;
        using variant_value  = std::expected<std::variant<tagged_marker, int>, int>;

        check_one_shot_reads<optional_value, codec::std_expected, markers>("82f507", [](const optional_value &value) {
            REQUIRE(value.has_value());
            CHECK(value->has_value());
        });
        check_one_shot_reads<array_value, codec::std_expected, markers>("82f59f0707ff", [](const array_value &value) {
            REQUIRE(value.has_value());
            CHECK(value->size() == 2);
        });
        check_one_shot_reads<variant_value, codec::std_expected, markers>("82f5d86407", [](const variant_value &value) {
            REQUIRE(value.has_value());
            CHECK(std::holds_alternative<tagged_marker>(*value));
        });

        // The codec must still reject a different marker and an incomplete integer.
        for (const auto hex : {"82f508", "82f51900"}) {
            CAPTURE(hex);
            const auto     bytes = to_bytes(hex);
            optional_value value;
            const auto     result = make_decoder<codec::std_expected, markers>(bytes)(value);
            REQUIRE_FALSE(result);
            CHECK(result.error() == (hex == std::string_view{"82f508"} ? status_code::error : status_code::incomplete));
        }
    }
#endif

    TEST_CASE("a decode(T &) codec reads a number where the decoder starts on it") { check_reads<float, centi_degrees>("190866", 21.5F); }

    TEST_CASE("a decode(T &) codec reads a number right inside an optional") {
        check_reads<std::optional<float>, centi_degrees>("190866", 21.5F);
    }

    TEST_CASE("a decode(T &) codec reads the elements of an array of known length") {
        check_reads<std::vector<float>, centi_degrees>("821908661908e3", {21.5F, 22.75F});
    }

    TEST_CASE("a decode(T &) codec reads the elements of an indefinite array") {
        check_reads<std::vector<float>, centi_degrees>("9f1908661908e3ff", {21.5F, 22.75F});
    }

    TEST_CASE("a decode(T &) codec reads the values of an indefinite map") {
        check_reads<std::map<int, float>, centi_degrees>("bf01190866021908e3ff", {{1, 21.5F}, {2, 22.75F}});
    }

    TEST_CASE("a decode(T &) codec reads the keys of an indefinite map") {
        check_reads<std::map<std::uint32_t, int>, offset_numbers>("bf01020304ff", {{1001U, 2}, {1003U, 4}});
    }

    TEST_CASE("a decode(T &) codec reads an alternative of a variant") {
        check_reads<std::variant<std::uint32_t, std::string>, offset_numbers>("07", std::uint32_t{1007});
    }

    TEST_CASE("a decode(T &) codec for the number an enum is made of reads the enum") {
        check_reads<level, offset_numbers>("07", level::low);

        // 1000 takes three bytes, so the step back crosses the pieces of a split input
        check_reads<level, offset_numbers>("1903e8", level::high);
        check_reads<std::optional<level>, offset_numbers>("1903e8", level::high);
        check_reads<std::vector<level>, offset_numbers>("9f071903e8ff", {level::low, level::high});
    }

    TEST_CASE("a decode(T &) codec reads a struct right inside an optional") {
        check_reads<std::optional<position>, y_first>("820703", position{3, 7});
    }

    TEST_CASE("a decode(T &) codec reads the structs of an indefinite array") {
        check_reads<std::vector<position>, y_first>("9f820703820905ff", {position{3, 7}, position{5, 9}});
    }

    TEST_CASE("a decode(T &) codec reads the only field of a struct whose header is read") {
        check_reads<std::optional<holder>, y_first>("820703", holder{position{3, 7}});
    }

    TEST_CASE("a decode(T &) codec reads a tagged alternative, with the tag in the first byte") {
        check_reads<std::variant<reading<10>, int>, offset_readings>("ca05", reading<10>{1005});
    }

    TEST_CASE("a decode(T &) codec reads a tagged alternative, with 1 more byte of tag") {
        check_reads<std::variant<reading<100>, int>, offset_readings>("d86405", reading<100>{1005});
    }

    TEST_CASE("a decode(T &) codec reads a tagged alternative, with 2 more bytes of tag") {
        check_reads<std::variant<reading<1000>, int>, offset_readings>("d903e805", reading<1000>{1005});
    }

    TEST_CASE("a decode(T &) codec reads a tagged alternative, with 4 more bytes of tag") {
        check_reads<std::variant<reading<100000>, int>, offset_readings>("da000186a005", reading<100000>{1005});
    }

    TEST_CASE("a decode(T &) codec reads a tagged alternative, with 8 more bytes of tag") {
        check_reads<std::variant<reading<4294967296>, int>, offset_readings>("db000000010000000005", reading<4294967296>{1005});
    }

    TEST_CASE("a decode(T &) codec reads a tagged alternative whose tag takes more bytes than it needs") {
        check_reads<std::variant<reading<100>, int>, offset_readings>("d9006405", reading<100>{1005});
    }

    TEST_CASE("the other alternative of that variant is read as before") {
        check_reads<std::variant<reading<100>, int>, offset_readings>("05", std::variant<reading<100>, int>{std::in_place_index<1>, 5});
    }

    TEST_CASE("a decode(T &) codec reads what a unique pointer points at") {
        using codec::unique_ptr;

        check_reads_as<std::unique_ptr<std::uint32_t>, unique_ptr, offset_numbers>("07", points_at<std::unique_ptr<std::uint32_t>>(1007));
        check_reads_as<std::unique_ptr<position>, unique_ptr, y_first>("820703", points_at<std::unique_ptr<position>>({3, 7}));

        // The pointer hands the header on to the only field of the struct
        check_reads_as<std::unique_ptr<holder>, unique_ptr, y_first>("820703", points_at<std::unique_ptr<holder>>({position{3, 7}}));
    }

    TEST_CASE("a decode(T &) codec reads the tagged struct a unique pointer points at") {
        using codec::unique_ptr;
        using pointer = std::unique_ptr<reading<100>>;

        check_reads_as<pointer, unique_ptr, offset_readings>("d86405", points_at<pointer>({1005}));
        check_reads_as<pointer, unique_ptr, offset_readings>("d9006405", points_at<pointer>({1005}));
    }

    TEST_CASE("a decode(T &) codec reads the unique pointers of an indefinite array") {
        using pointers = std::vector<std::unique_ptr<position>>;

        check_reads_as<pointers, codec::unique_ptr, y_first>("9f820703820905ff", [](const pointers &values) {
            REQUIRE(values.size() == 2);
            REQUIRE(values[0]);
            REQUIRE(values[1]);
            CHECK(*values[0] == position{3, 7});
            CHECK(*values[1] == position{5, 9});
        });
    }

    TEST_CASE("a decode(T &) codec reads what a shared pointer points at") {
        using codec::shared_ptr;

        // Tag 28 goes around what the pointer shares. No resumable side for this codec
        check_one_shot_reads<std::shared_ptr<position>, shared_ptr, y_first>("d81c820703", points_at<std::shared_ptr<position>>({3, 7}));
        check_one_shot_reads<std::shared_ptr<reading<100>>, shared_ptr, offset_readings>("d81cd86405",
                                                                                         points_at<std::shared_ptr<reading<100>>>({1005}));
    }

#if defined(__cpp_lib_indirect) && __cpp_lib_indirect >= 202502L
    TEST_CASE("a decode(T &) codec reads what a std::indirect holds") {
        using held = std::indirect<position>;

        check_one_shot_reads<held, codec::std_indirect, y_first>("820703", [](const held &value) { CHECK(*value == position{3, 7}); });
    }

    TEST_CASE("std indirect accepts a complete item supplied only by a first-byte codec") {
        using held = std::indirect<marker>;
        check_one_shot_reads<held, codec::std_indirect, markers>("07",
                                                                 [](const held &value) { CHECK_FALSE(value.valueless_after_move()); });
    }
#endif

    TEST_CASE("a codec with both forms gets the one for where the decoder stands") {
        // From the first byte
        check_reads<std::uint32_t, marked_numbers>("07", std::uint32_t{1007});

        // With the header read
        check_reads<std::optional<std::uint32_t>, marked_numbers>("07", std::uint32_t{2007});
        check_reads<std::vector<std::uint32_t>, marked_numbers>("9f0708ff", {2007U, 2008U});
        check_reads<std::variant<std::uint32_t, std::string>, marked_numbers>("07", std::uint32_t{2007});
    }
}
