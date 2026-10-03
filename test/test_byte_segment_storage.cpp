#include <algorithm>
#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_segments.h>
#include <cstddef>
#include <doctest/doctest.h>
#include <memory>
#include <ranges>
#include <span>
#include <vector>

using namespace cbor::tags;

namespace {

std::vector<std::byte> patterned_bytes(std::size_t size) {
    std::vector<std::byte> result(size);
    for (std::size_t index = 0; index < size; ++index) {
        result[index] = static_cast<std::byte>((index * 37 + 19) & 255);
    }
    return result;
}

struct appending_byte_segment_storage {
    using segment_type   = basic_byte_segment<4>;
    using container_type = std::vector<segment_type>;

    int *append_calls{};
    int *make_owned_calls{};

    container_type make_container() const { return {}; }
    segment_type   make_owned(std::span<const std::byte> bytes) const {
        ++*make_owned_calls;
        return segment_type::owned(bytes);
    }
    segment_type make_borrowed(std::span<const std::byte> bytes) const { return segment_type::borrowed(bytes); }

    bool try_append_owned(container_type &segments, std::span<const std::byte> bytes) {
        ++*append_calls;
        return !segments.empty() && segments.back().append_owned(bytes);
    }
};

struct shared_byte_view : std::ranges::view_base {
    std::shared_ptr<const std::vector<std::byte>> storage;

    auto begin() const noexcept { return storage->begin(); }
    auto end() const noexcept { return storage->end(); }
    auto data() const noexcept { return storage->data(); }
    auto size() const noexcept { return storage->size(); }
};

struct byte_payload_frame {
    std::vector<std::byte> routing;
    std::vector<std::byte> payload;

    bool operator==(const byte_payload_frame &) const = default;
};

} // namespace

TEST_SUITE("segments/storage") {

    TEST_CASE("dynamic owned segments copy and append independent source bytes") {
        auto       source   = patterned_bytes(4096);
        const auto original = source;
        auto       segment  = byte_segment::owned(source);

        std::ranges::fill(source, std::byte{});
        CHECK(segment.is_owned());
        CHECK_EQ(segment.size(), original.size());
        CHECK(std::ranges::equal(segment.bytes(), original));

        auto       suffix   = patterned_bytes(4113);
        const auto appended = suffix;
        REQUIRE(segment.append_owned(suffix));
        std::ranges::fill(suffix, std::byte{});

        REQUIRE_EQ(segment.size(), original.size() + appended.size());
        CHECK(std::ranges::equal(segment.bytes().first(original.size()), original));
        CHECK(std::ranges::equal(segment.bytes().subspan(original.size()), appended));
    }

    TEST_CASE("owned segment promotion preserves the inline prefix and copies the suffix") {
        const auto prefix  = patterned_bytes(byte_segment::inline_owned_capacity);
        auto       suffix  = patterned_bytes(4096);
        const auto copied  = suffix;
        auto       segment = byte_segment::owned(prefix);

        REQUIRE(segment.append_owned(suffix));
        std::ranges::fill(suffix, std::byte{});

        REQUIRE_EQ(segment.size(), prefix.size() + copied.size());
        CHECK(std::ranges::equal(segment.bytes().first(prefix.size()), prefix));
        CHECK(std::ranges::equal(segment.bytes().subspan(prefix.size()), copied));
    }

    TEST_CASE("owned segment promotion accepts a source in its inline bytes") {
        const auto prefix  = patterned_bytes(byte_segment::inline_owned_capacity);
        auto       segment = byte_segment::owned(prefix);

        REQUIRE(segment.append_owned(segment.bytes().subspan(8, 16)));

        REQUIRE_EQ(segment.size(), prefix.size() + 16U);
        CHECK(std::ranges::equal(segment.bytes().first(prefix.size()), prefix));
        CHECK(std::ranges::equal(segment.bytes().subspan(prefix.size()), std::span{prefix}.subspan(8, 16)));
    }

    TEST_CASE("zero inline capacity segments handle empty and dynamic owned bytes") {
        auto segment = basic_byte_segment<0>::owned(std::span<const std::byte>{});
        CHECK(segment.empty());
        REQUIRE(segment.append_owned(std::span<const std::byte>{}));

        const std::array source{std::byte{7}, std::byte{3}, std::byte{9}};
        REQUIRE(segment.append_owned(source));
        REQUIRE(segment.append_owned(std::span<const std::byte>{}));
        CHECK(std::ranges::equal(segment.bytes(), source));

        auto borrowed = basic_byte_segment<0>::borrowed(source);
        CHECK_FALSE(borrowed.append_owned(source));
        CHECK_EQ(borrowed.data(), source.data());
        CHECK(std::ranges::equal(borrowed.bytes(), source));
    }

    TEST_CASE("custom storage append hooks coalesce dynamic owned bytes") {
        int                                                 append_calls{};
        int                                                 make_owned_calls{};
        basic_byte_segments<appending_byte_segment_storage> segments{appending_byte_segment_storage{&append_calls, &make_owned_calls}};
        const auto                                          prefix = patterned_bytes(4096);
        const auto                                          suffix = patterned_bytes(67);

        segments.append_owned(prefix);
        segments.append_owned(suffix);

        CHECK_EQ(append_calls, 2);
        CHECK_EQ(make_owned_calls, 1);
        REQUIRE_EQ(segments.size(), 1U);
        REQUIRE_EQ(segments.front().size(), prefix.size() + suffix.size());
        CHECK(std::ranges::equal(segments.front().bytes().first(prefix.size()), prefix));
        CHECK(std::ranges::equal(segments.front().bytes().subspan(prefix.size()), suffix));
    }

    TEST_CASE("segmented encoder roundtrips an owned payload after repeated promotion and growth") {
        std::vector<int> input(4096);
        for (std::size_t index = 0; index < input.size(); ++index) {
            input[index] = static_cast<int>(index) - 2048;
        }
        basic_byte_segments<default_byte_segment_storage<4>> segments;
        auto                                                 encoder = make_encoder(segments);
        REQUIRE(encoder(input));
        REQUIRE_EQ(segments.size(), 1U);
        CHECK(segments.front().is_owned());

        const auto       encoded = segments.flatten();
        std::vector<int> output;
        auto             decoder = make_decoder(encoded);
        REQUIRE(decoder(output));
        CHECK(output == input);
        CHECK(decoder.tell() == encoded.end());
    }

    TEST_CASE("segmented encoder copies an owning encoded item after a borrowed item") {
        const byte_payload_frame expected{{std::byte{7}, std::byte{3}, std::byte{9}}, patterned_bytes(4096)};
        std::vector<std::byte>   routing_item;
        auto                     routing_encoder = make_encoder(routing_item);
        REQUIRE(routing_encoder(expected.routing));

        byte_segments segments;
        {
            std::vector<std::byte> payload_item;
            auto                   payload_encoder = make_encoder(payload_item);
            REQUIRE(payload_encoder(expected.payload));

            shared_byte_view owner;
            owner.storage = std::make_shared<const std::vector<std::byte>>(std::move(payload_item));
            const basic_encoded_item_view<shared_byte_view> item{std::move(owner)};
            auto                                            encoder = make_encoder(segments);
            REQUIRE(encoder(as_array{2}, encoded_item_view{std::span<const std::byte>{routing_item}}, item));

            REQUIRE_EQ(segments.size(), 3U);
            CHECK(segments[1].is_borrowed());
            CHECK_EQ(segments[1].data(), routing_item.data());
            CHECK(segments[2].is_owned());
            CHECK_NE(segments[2].data(), item.span().data());
        }

        const auto         encoded = segments.flatten();
        byte_payload_frame actual;
        auto               decoder = make_decoder(encoded);
        REQUIRE(decoder(actual));
        CHECK(actual == expected);
        CHECK(decoder.tell() == encoded.end());
    }

} // TEST_SUITE("segments/storage")
