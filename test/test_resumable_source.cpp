#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cbor_tags/detail/cbor_resumable_source.h>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <iterator>
#include <list>
#include <ranges>
#include <span>
#include <vector>

namespace ct = cbor::tags;

namespace {

using input_storage  = std::list<std::span<const std::byte>>;
using output_storage = std::list<std::span<std::byte>>;
using input_type     = decltype(ct::make_input_source(std::declval<input_storage &>()));
using output_type    = decltype(ct::make_output_source(std::declval<output_storage &>()));

static_assert(ct::CborInputBuffer<input_type>);
static_assert(std::ranges::bidirectional_range<const input_type>);
static_assert(std::ranges::sized_range<const input_type>);
static_assert(!std::ranges::contiguous_range<input_type>);
static_assert(ct::CborOutputBuffer<output_type>);
static_assert(std::ranges::sized_range<const output_type>);

template <typename Source, typename Chunk>
concept source_accepts = requires(Source &source, Chunk &&chunk) { source.append(std::forward<Chunk>(chunk)); };

static_assert(source_accepts<input_type, std::vector<std::byte> &>);
static_assert(!source_accepts<input_type, std::vector<std::byte>>);
static_assert(source_accepts<input_type, std::span<const std::byte>>);

template <typename Range> auto bytes_of(Range &&range) {
    return std::vector<std::byte>(std::ranges::begin(range), std::ranges::end(range));
}

// Count traversal independently of source offsets to detect hidden prewalks.
struct counted_iterator {
    using value_type        = std::byte;
    using difference_type   = std::ptrdiff_t;
    using iterator_category = std::bidirectional_iterator_tag;
    using iterator_concept  = std::bidirectional_iterator_tag;

    std::list<std::byte>::iterator position;
    std::size_t                   *increments{};

    std::byte        &operator*() const { return *position; }
    counted_iterator &operator++() {
        ++*increments;
        ++position;
        return *this;
    }
    counted_iterator operator++(int) {
        auto previous = *this;
        ++*this;
        return previous;
    }
    counted_iterator &operator--() {
        --position;
        return *this;
    }
    counted_iterator operator--(int) {
        auto previous = *this;
        --*this;
        return previous;
    }
    friend bool operator==(const counted_iterator &left, const counted_iterator &right) { return left.position == right.position; }
};

using unsized_chunk   = std::ranges::subrange<counted_iterator>;
using unsized_storage = std::list<unsized_chunk>;
using unsized_source  = decltype(ct::make_input_source(std::declval<unsized_storage &>()));

static_assert(std::bidirectional_iterator<counted_iterator>);
static_assert(!std::ranges::sized_range<unsized_chunk>);
static_assert(ct::CborInputBuffer<unsized_source>);
static_assert(!std::ranges::sized_range<unsized_source>);

} // namespace

TEST_SUITE("resumable/source") {

    TEST_CASE("integer arrays traverse admitted unsized chunks once") {
        const std::vector<std::int64_t> expected{0, 24, -25, 256, -65537, 4294967296LL};
        std::vector<std::byte>          wire;
        REQUIRE(ct::make_encoder(wire)(expected));
        std::list<std::byte> bytes(wire.begin(), wire.end());
        const auto           middle = std::next(bytes.begin(), wire.size() / 2);
        std::size_t          increments{};
        unsized_storage      storage;
        auto                 source = ct::make_input_source(storage);

        // Admit the complete item before decoding, so retries cannot add legitimate traversal.
        source.append(unsized_chunk{counted_iterator{bytes.begin(), &increments}, counted_iterator{middle, &increments}},
                      unsized_chunk{counted_iterator{middle, &increments}, counted_iterator{bytes.end(), &increments}});
        CHECK(increments == 0U);
        std::vector<std::int64_t> actual;
        auto                      operation = ct::make_resumable_decoder(source)(actual);
        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(*operation.result() == wire.size());
        CHECK(actual == expected);
        CHECK(increments == wire.size());
    }

    TEST_CASE("input cursors survive empty chunks and later admission") {
        std::array    first{std::byte{1}, std::byte{2}};
        std::array    second{std::byte{3}, std::byte{4}};
        input_storage storage;
        auto          source = ct::make_input_source(storage);
        auto          cursor = source.begin();
        CHECK(cursor == source.end());

        const auto empty_generation = source.generation();
        CHECK(source.append(std::span<const std::byte>{}) == 0);
        CHECK(source.generation() == empty_generation);
        CHECK(storage.empty());

        source.append(std::span<const std::byte>{}, first);
        CHECK(source.size() == 2);
        CHECK(*cursor == std::byte{1});
        CHECK(source.advance(cursor, 2) == 2);
        CHECK(cursor == source.end());
        CHECK(cursor.offset() == 2);
        source.commit(cursor);

        std::array batch{std::span<const std::byte>{}, std::span<const std::byte>(second), std::span<const std::byte>{}};
        source.append(batch);
        CHECK(*cursor == std::byte{3});
        CHECK(source.committed_position().offset() == 2);
        CHECK(bytes_of(source.remaining(cursor)) == std::vector<std::byte>(second.begin(), second.end()));

        auto tail = source.end();
        --tail;
        CHECK(*tail == std::byte{4});
        CHECK(tail.offset() == 3);
        --tail;
        --tail;
        CHECK(*tail == std::byte{2});

        source.acquire();
        CHECK_THROWS_AS(source.acquire(), std::logic_error);
        source.release();
        CHECK_NOTHROW(source.acquire());
        source.release();

        const auto generation = source.generation();
        source.close();
        CHECK(source.eof());
        CHECK(source.generation() > generation);
        CHECK_THROWS_AS(source.append(first), std::logic_error);
    }

    TEST_CASE_TEMPLATE("input descriptors can relocate without invalidating checkpoints", Storage, std::vector<std::span<const std::byte>>,
                       std::deque<std::span<const std::byte>>) {
        std::array bytes{std::byte{5}, std::byte{6}, std::byte{7}};
        Storage    storage;
        auto       source = ct::make_input_source(storage);
        source.append(bytes);

        auto cursor = source.begin();
        ++cursor;
        auto checkpoint = cursor;
        source.advance(cursor, 2);
        REQUIRE(cursor == source.end());

        for (int i = 0; i != 256; ++i) {
            source.append(bytes);
        }
        CHECK(*checkpoint == std::byte{6});
        CHECK(*cursor == std::byte{5});
        CHECK(checkpoint.offset() == 1);
        CHECK(cursor.offset() == 3);
        CHECK(source.size() == 257 * bytes.size());
    }

    TEST_CASE("noncontiguous input preserves inner iterator traversal") {
        std::list<std::byte> first(64, std::byte{9});
        std::list<std::byte> second(32, std::byte{7});
        std::size_t          increments{};
        unsized_storage      storage;
        auto                 source = ct::make_input_source(storage);
        source.append(unsized_chunk{counted_iterator{first.begin(), &increments}, counted_iterator{first.end(), &increments}});
        source.append(unsized_chunk{counted_iterator{second.begin(), &increments}, counted_iterator{second.end(), &increments}});
        CHECK(increments == 0);

        auto cursor = source.begin();
        CHECK_FALSE(source.available_span(cursor).has_value());
        CHECK(source.advance(cursor, 64) == 64);
        CHECK(increments == 64);
        CHECK(*cursor == std::byte{7});
        CHECK(source.advance(cursor, 40) == 32);
        CHECK(increments == 96);
        CHECK(cursor.offset() == 96);
        CHECK(cursor == source.end());

        --cursor;
        CHECK(*cursor == std::byte{7});
        CHECK(cursor.offset() == 95);
        auto last = source.end();
        --last;
        CHECK(last == cursor);
    }

    TEST_CASE("fixed descriptor spans provide input without owning storage") {
        std::array first{std::byte{1}, std::byte{2}};
        std::array second{std::byte{3}};
        std::array descriptors{std::span<const std::byte>{}, std::span<const std::byte>(first), std::span<const std::byte>{},
                               std::span<const std::byte>(second), std::span<const std::byte>{}};
        auto       storage = std::span(descriptors);
        auto       source  = ct::make_input_source(storage);
        CHECK(source.size() == 3);
        auto cursor = source.begin();
        REQUIRE(source.available_span(cursor).has_value());
        CHECK(source.available_span(cursor)->size() == 2);
        source.advance(cursor, 2);
        CHECK(source.available_span(cursor)->size() == 1);
        CHECK(*cursor == std::byte{3});
        auto last = source.end();
        --last;
        CHECK(*last == std::byte{3});
    }

    TEST_CASE("output retries overwrite tentative bytes and expose only the written prefix") {
        std::array<std::byte, 2> first{};
        std::array<std::byte, 3> second{};
        output_storage           storage;
        auto                     source = ct::make_output_source(storage);
        source.append(first);
        auto checkpoint = source.position();

        const std::array values{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
        CHECK_THROWS_AS(source.write(values), ct::detail::encode_status_exception);
        CHECK(source.size() == 2);
        CHECK(source.committed_position().offset() == 0);

        source.restore(checkpoint);
        CHECK(source.size() == 0);
        CHECK(source.begin() == source.end());
        source.append(second);
        source.write(values);
        source.commit();
        CHECK(source.size() == values.size());
        CHECK(source.committed_position().offset() == values.size());
        CHECK(bytes_of(source.produced(checkpoint, source.position())) == std::vector<std::byte>(values.begin(), values.end()));
        CHECK(bytes_of(source) == std::vector<std::byte>(values.begin(), values.end()));
        CHECK(std::ranges::distance(source.remaining_output()) == 1);
    }

    TEST_CASE("normal codecs operate on typed input and output sources") {
        std::deque<unsigned char>                                   first(2);
        std::deque<unsigned char>                                   second(32);
        std::list<std::ranges::ref_view<std::deque<unsigned char>>> storage;
        auto                                                        output = ct::make_output_source(storage);
        output.append(first, second);

        const std::vector<std::uint32_t> values{1, 24, 1024};
        auto                             encoder = ct::make_encoder(output);
        REQUIRE(encoder(values));
        output.commit();

        std::list<decltype(output.produced(output.begin(), output.position()))> input_descriptors;
        input_descriptors.push_back(output.produced(output.begin(), output.position()));
        auto                       input   = ct::make_input_source(input_descriptors);
        auto                       decoder = ct::make_decoder(input);
        std::vector<std::uint32_t> decoded;
        REQUIRE(decoder(decoded));
        CHECK(decoded == values);
    }

    TEST_CASE("ordinary encoder capacity failure can retry from its saved position") {
        std::array<std::byte, 2>          first{};
        std::array<std::byte, 16>         second{};
        std::vector<std::span<std::byte>> storage;
        auto                              output = ct::make_output_source(storage);
        output.append(first);
        const auto checkpoint = output.position();

        auto       encoder      = ct::make_encoder(output);
        const auto value        = std::numeric_limits<std::uint64_t>::max();
        const auto short_result = encoder(value);
        REQUIRE_FALSE(short_result.has_value());
        CHECK(short_result.error() == ct::status_code::incomplete);
        CHECK(output.size() == first.size());

        output.restore(checkpoint);
        output.append(second);
        REQUIRE(encoder(value));
        output.commit();

        auto          decoder = ct::make_decoder(output);
        std::uint64_t decoded{};
        REQUIRE(decoder(decoded));
        CHECK(decoded == value);
    }

} // TEST_SUITE
