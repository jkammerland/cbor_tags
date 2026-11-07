#include "character_traits.h"
#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_raw_views.h>
#include <cbor_tags/cbor_resumable.h>

#include <cstdint>
#include <deque>
#include <list>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace cbor::tags;

namespace {

using input_chunks = std::list<std::span<const std::byte>>;

template <typename Factory>
concept has_view_types = requires {
    typename Factory::bstr_view_t;
    typename Factory::tstr_view_t;
    typename Factory::encoded_item_view;
    typename Factory::encoded_array_view;
    typename Factory::encoded_map_view;
};

// Both decoder factory kinds name their views, encoders have none.
static_assert(has_view_types<decltype(make_resumable_decoder(std::declval<std::vector<std::byte> &>()))>);
static_assert(has_view_types<decltype(make_resumable_decoder(std::declval<decltype(make_input_source(std::declval<input_chunks &>())) &>()))>);
static_assert(!has_view_types<decltype(make_resumable_encoder(std::declval<std::vector<std::byte> &>()))>);

template <typename T> std::vector<std::byte> encode_one(const T &value) {
    std::vector<std::byte> wire;
    REQUIRE(make_encoder(wire)(value));
    return wire;
}

template <typename Range> std::vector<std::byte> copy_bytes(const Range &range) { return {range.begin(), range.end()}; }

// What the views should hold, each piece encoded on its own.
struct borrowed_message {
    std::string                text = "station";
    std::vector<std::byte>     blob;
    std::vector<std::uint32_t> values{1, 10, 1000, 100000};
    std::map<std::string, int> entries{{"a", 1}, {"b", 2}};
    int                        tail = 1000;

    borrowed_message() {
        for (std::size_t index = 0; index < 300; ++index)
            blob.push_back(static_cast<std::byte>(index & 0xff));
    }

    std::vector<std::byte> wire() const {
        std::vector<std::byte> bytes;
        REQUIRE(make_encoder(bytes)(text, blob, values, entries, tail));
        return bytes;
    }
};

template <typename Text, typename Blob, typename Array, typename Map, typename Item>
void check_views(const borrowed_message &sent, const Text &text, const Blob &blob, const Array &array, const Map &map, const Item &item) {
    CHECK(std::string(text) == sent.text);
    CHECK(std::vector<std::byte>(blob) == sent.blob);
    CHECK(copy_bytes(array.bytes()) == encode_one(sent.values));
    CHECK(copy_bytes(map.bytes()) == encode_one(sent.entries));
    CHECK(copy_bytes(item.bytes()) == encode_one(sent.tail));
}

} // namespace

TEST_SUITE("cbor_wire/resumable") {

    TEST_CASE("decoder view aliases borrow from chunked input at every split") {
        const borrowed_message sent;
        const auto             wire = sent.wire();

        for (std::size_t cut = 0; cut <= wire.size(); ++cut) {
            CAPTURE(cut);
            input_chunks storage;
            auto         source  = make_input_source(storage);
            auto         decoder = make_resumable_decoder(source);

            decltype(decoder)::tstr_view_t        text;
            decltype(decoder)::bstr_view_t        blob;
            decltype(decoder)::encoded_array_view array;
            decltype(decoder)::encoded_map_view   map;
            decltype(decoder)::encoded_item_view  item;
            {
                auto operation = decoder(text, blob, array, map, item);
                storage.emplace_back(std::span<const std::byte>{wire}.first(cut));
                operation.resume();
                storage.emplace_back(std::span<const std::byte>{wire}.subspan(cut));
                operation.resume();
                REQUIRE(operation.done());
                REQUIRE(operation.result());
                CHECK(*operation.result() == wire.size());
            }

            // The views point into the source, not the operation.
            check_views(sent, text, blob, array, map, item);
        }
    }

    TEST_CASE("decoder view aliases survive storage reallocation") {
        const borrowed_message sent;
        const auto             wire = sent.wire();

        std::vector<std::byte> input;
        auto                   decoder = make_resumable_decoder(input);

        decltype(decoder)::tstr_view_t        text;
        decltype(decoder)::bstr_view_t        blob;
        decltype(decoder)::encoded_array_view array;
        decltype(decoder)::encoded_map_view   map;
        decltype(decoder)::encoded_item_view  item;
        {
            // One byte per resume, so the vector reallocates under the decode.
            auto operation = decoder(text, blob, array, map, item);
            for (const auto byte : wire) {
                REQUIRE_FALSE(operation.done());
                input.push_back(byte);
                operation.resume();
            }
            REQUIRE(operation.done());
            REQUIRE(operation.result());
        }

        input.reserve(input.capacity() * 2 + 1);
        check_views(sent, text, blob, array, map, item);
    }

    TEST_CASE("contiguous borrowed views fail on the header instead of waiting for the payload") {
        // Every resumable source is non-contiguous, so these can never succeed.
        // The payload is 3 bytes and only 1 is there.
        const auto bytes = to_bytes("4361");
        const auto text  = to_bytes("6361");

        auto check = [](std::span<const std::byte> wire, auto &&target) {
            input_chunks storage{wire};
            auto         source    = make_input_source(storage);
            auto         operation = make_resumable_decoder(source)(target);
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::contiguous_view_on_non_contiguous_data);
        };

        std::span<const std::byte> span;
        check(bytes, span);
        check(bytes, as_bounded_size<1, 4>(span));
        check(bytes, as_bounded_size(span, 1, 4));
        CHECK(span.empty());

        test_util::basic_string_view<std::byte> byte_view;
        check(bytes, byte_view);

        std::string_view string_view;
        check(text, string_view);
        check(text, as_bounded_size<1, 4>(string_view));
        CHECK(string_view.empty());
    }
}
