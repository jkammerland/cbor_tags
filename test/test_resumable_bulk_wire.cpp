#include <array>
#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <limits>
#include <list>
#include <new>
#include <ranges>
#include <span>
#include <stdexcept>
#include <vector>

namespace ct = cbor::tags;

namespace {

// These deliberately unrelated helper names must not opt a user source into
// the built-in iterator's endian and bulk-copy implementation.
class named_helper_source {
  public:
    struct position_type {
        using value_type        = std::byte;
        using difference_type   = std::ptrdiff_t;
        using iterator_category = std::bidirectional_iterator_tag;
        using iterator_concept  = std::bidirectional_iterator_tag;
        const named_helper_source *source{};
        std::size_t                index{};

        const std::byte &operator*() const { return source->bytes_[index]; }
        position_type   &operator++() {
            ++index;
            return *this;
        }
        position_type operator++(int) {
            auto old = *this;
            ++*this;
            return old;
        }
        position_type &operator--() {
            --index;
            return *this;
        }
        position_type operator--(int) {
            auto old = *this;
            --*this;
            return old;
        }
        std::size_t offset() const { return index; }
        friend bool operator==(position_type, position_type) = default;

        std::uint64_t read_unsigned(std::size_t count) {
            ++source->unrelated_calls;
            index += count;
            return 0;
        }
        std::size_t read_bytes(std::span<std::byte> bytes) {
            ++source->unrelated_calls;
            std::ranges::fill(bytes, std::byte{});
            index += bytes.size();
            return bytes.size();
        }
    };

    explicit named_helper_source(std::span<const std::byte> bytes) : bytes_(bytes) {}
    position_type begin() const { return {this, 0}; }
    position_type end() const { return {this, bytes_.size()}; }
    position_type committed_position() const noexcept { return {this, committed_}; }
    void          commit(position_type position) { committed_ = position.index; }
    auto          remaining(position_type position) const { return std::ranges::subrange(position, end()); }
    void          refresh() {}
    bool          eof() const { return closed_; }
    void          close() { closed_ = true; }
    void          acquire() {
        if (active_)
            throw std::logic_error("source already active");
        active_ = true;
    }
    void                release() noexcept { active_ = false; }
    mutable std::size_t unrelated_calls{};

  private:
    std::span<const std::byte> bytes_;
    std::size_t                committed_{};
    bool                       closed_{};
    bool                       active_{};
};

static_assert(ct::ResumableInputSource<named_helper_source>);

// A transport may accept a prefix even when its backing container can grow.
class bounded_write_source : public ct::output_source<std::vector<std::byte>> {
  public:
    using output_source::output_source;

    std::optional<std::span<std::byte>> borrow_window() { return std::nullopt; }
    std::size_t                         write(std::span<const std::byte> bytes) {
        if (allowance == 0)
            ++writes_after_exhaustion;
        const auto count = output_source::write(bytes.first(std::min(bytes.size(), allowance)));
        allowance -= count;
        return count;
    }

    std::size_t allowance{};
    std::size_t writes_after_exhaustion{};
};

static_assert(ct::ResumableOutputSource<bounded_write_source>);

// Optional windows do not replace a custom source's write contract.
class custom_empty_window_source {
    ct::output_source<std::vector<std::byte>> source_;

  public:
    using value_type    = std::byte;
    using size_type     = std::size_t;
    using position_type = ct::output_source<std::vector<std::byte>>::position_type;

    explicit custom_empty_window_source(std::vector<std::byte> &storage) : source_(storage) {}
    auto begin() const { return source_.begin(); }
    auto end() const { return source_.end(); }
    auto size() const { return source_.size(); }
    auto position() const { return source_.position(); }
    auto committed_position() const noexcept { return source_.committed_position(); }
    auto remaining_output() const { return source_.remaining_output(); }
    void commit() { source_.commit(); }
    void restore(position_type position) noexcept { source_.restore(position); }
    void refresh() { source_.refresh(); }
    bool eof() const { return source_.eof(); }
    void close() { source_.close(); }
    void acquire() { source_.acquire(); }
    void release() noexcept { source_.release(); }

    std::optional<std::span<std::byte>> borrow_window() { return std::span<std::byte>{}; }
    std::size_t                         write(std::span<const std::byte> bytes) {
        ++writes;
        return source_.write(bytes);
    }

    std::size_t writes{};
};

class derived_empty_window_source : public ct::output_source<std::vector<std::byte>> {
  public:
    using output_source::output_source;

    std::optional<std::span<std::byte>> borrow_window() { return std::span<std::byte>{}; }
    std::size_t                         write(std::span<const std::byte> bytes) {
        ++writes;
        return output_source::write(bytes);
    }

    std::size_t writes{};
};

static_assert(ct::ResumableOutputSource<custom_empty_window_source>);
static_assert(ct::ResumableOutputSource<derived_empty_window_source>);

// Count native traversal separately from the adapter's offsets and boundaries.
struct node_read_counts {
    std::size_t increments{};
    std::size_t dereferences{};
    std::size_t ends{};
    std::size_t throw_at = std::numeric_limits<std::size_t>::max();
};

struct node_read_iterator {
    using value_type        = std::byte;
    using difference_type   = std::ptrdiff_t;
    using iterator_category = std::bidirectional_iterator_tag;
    using iterator_concept  = std::bidirectional_iterator_tag;

    std::list<std::byte>::iterator position;
    node_read_counts              *counts{};

    std::byte &operator*() const {
        if (counts->dereferences == counts->throw_at)
            throw std::bad_alloc{};
        ++counts->dereferences;
        return *position;
    }
    node_read_iterator &operator++() {
        ++counts->increments;
        ++position;
        return *this;
    }
    node_read_iterator operator++(int) {
        auto old = *this;
        ++*this;
        return old;
    }
    node_read_iterator &operator--() {
        --position;
        return *this;
    }
    node_read_iterator operator--(int) {
        auto old = *this;
        --*this;
        return old;
    }
    friend bool operator==(const node_read_iterator &left, const node_read_iterator &right) { return left.position == right.position; }
};

struct counted_node_chunk {
    std::list<std::byte> bytes;
    node_read_counts    *counts;

    node_read_iterator begin() { return {bytes.begin(), counts}; }
    node_read_iterator end() {
        ++counts->ends;
        return {bytes.end(), counts};
    }
    std::size_t size() const { return bytes.size(); }
};

static_assert(std::ranges::bidirectional_range<counted_node_chunk>);
static_assert(std::ranges::sized_range<counted_node_chunk>);
static_assert(!std::ranges::random_access_range<counted_node_chunk>);

// Relocation must move nodes even when the library's list move may throw.
struct movable_bulk_chunk {
    std::list<std::byte> bytes;

    movable_bulk_chunk() = default;
    template <typename Iterator> movable_bulk_chunk(Iterator first, Iterator last) : bytes(first, last) {}
    movable_bulk_chunk(const movable_bulk_chunk &)            = delete;
    movable_bulk_chunk &operator=(const movable_bulk_chunk &) = delete;
    movable_bulk_chunk(movable_bulk_chunk &&)                 = default;
    movable_bulk_chunk &operator=(movable_bulk_chunk &&)      = default;

    auto        begin() { return bytes.begin(); }
    auto        end() { return bytes.end(); }
    std::size_t size() const { return bytes.size(); }
};

std::vector<std::byte> node_payload() {
    std::vector<std::byte> value(600);
    for (std::size_t index = 0; index != value.size(); ++index)
        value[index] = static_cast<std::byte>(index * 17 + 3);
    return value;
}

std::vector<std::byte> node_payload_wire(const std::vector<std::byte> &value) {
    // Definite byte string length 600 followed by a distinct unconsumed null.
    std::vector<std::byte> wire{std::byte{0x59}, std::byte{0x02}, std::byte{0x58}};
    wire.insert(wire.end(), value.begin(), value.end());
    wire.push_back(std::byte{0xf6});
    return wire;
}

} // namespace

TEST_SUITE("cbor_wire/resumable_bulk") {
    TEST_CASE("bounded empty headers suspend and partial headers remain atomic") {
        std::array<std::byte, 24> value;
        value.fill(std::byte{0x2a});
        std::array<std::byte, 26> output;
        output.fill(std::byte{0xcc});
        std::deque<std::span<std::byte>> chunks;
        auto                             source    = ct::make_output_source(chunks);
        auto                             encoder   = ct::make_resumable_encoder(source);
        auto                             operation = encoder(value);

        REQUIRE(operation.state() == ct::resume_state::need_output);
        CHECK(operation.committed_bytes() == 0);
        CHECK(source.position().offset() == 0);
        chunks.emplace_back();
        REQUIRE(operation.resume() == ct::resume_state::need_output);
        CHECK(operation.committed_bytes() == 0);

        // One byte cannot commit the two-byte length header.
        chunks.emplace_back(std::span{output}.first(1));
        REQUIRE(operation.resume() == ct::resume_state::need_output);
        CHECK(operation.committed_bytes() == 0);
        CHECK(source.position().offset() == 0);

        chunks.emplace_back(std::span{output}.subspan(1));
        REQUIRE(operation.resume() == ct::resume_state::done);
        REQUIRE(operation.result());
        CHECK(operation.committed_bytes() == output.size());
        CHECK(output[0] == std::byte{0x58});
        CHECK(output[1] == std::byte{0x18});
        CHECK(std::ranges::equal(std::span{output}.subspan(2), value));
    }

    TEST_CASE("growable empty headers retain the append path") {
        std::vector<std::byte> output{std::byte{0xf6}};
        auto                   encoder = ct::make_resumable_encoder(output);
        const std::array       value{std::byte{1}, std::byte{2}};
        auto                   operation = encoder(value);

        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(operation.committed_bytes() == 3);
        CHECK(output == std::vector<std::byte>{std::byte{0xf6}, std::byte{0x42}, std::byte{1}, std::byte{2}});
    }

    TEST_CASE_TEMPLATE("custom empty windows do not suppress header writes", Source, custom_empty_window_source,
                       derived_empty_window_source) {
        std::vector<std::byte> output;
        Source                 source(output);
        auto                   encoder = ct::make_resumable_encoder(source);
        const std::array       value{std::byte{1}, std::byte{2}};
        auto                   operation = encoder(value);

        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(source.writes > 0);
        CHECK(operation.committed_bytes() == 3);
        CHECK(output == std::vector<std::byte>{std::byte{0x42}, std::byte{1}, std::byte{2}});
    }

    TEST_CASE("closed empty output retains the incomplete header status") {
        for (const bool close_before_start : {false, true}) {
            CAPTURE(close_before_start);
            std::deque<std::span<std::byte>> chunks;
            auto                             source = ct::make_output_source(chunks);
            if (close_before_start)
                source.close();
            auto             encoder = ct::make_resumable_encoder(source);
            const std::array value{std::byte{1}};
            auto             operation = encoder(value);

            if (!close_before_start) {
                REQUIRE(operation.state() == ct::resume_state::need_output);
                REQUIRE(operation.finish() == ct::resume_state::done);
            }
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == ct::status_code::incomplete);
            CHECK(operation.committed_bytes() == 0);
            CHECK(source.position().offset() == 0);
        }
    }

    TEST_CASE("a short payload write suspends without probing the exhausted source again") {
        std::vector<std::byte> storage;
        bounded_write_source   source(storage);
        auto                   encoder = ct::make_resumable_encoder(source);
        const std::array       value{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};
        source.allowance = 3;
        auto operation   = encoder(value);

        REQUIRE(operation.state() == ct::resume_state::need_output);
        CHECK(operation.committed_bytes() == 3);
        CHECK(source.writes_after_exhaustion == 0);
        CHECK(storage == std::vector<std::byte>{std::byte{0x45}, std::byte{1}, std::byte{2}});

        source.allowance = 3;
        REQUIRE(operation.resume() == ct::resume_state::done);
        REQUIRE(operation.result());
        CHECK(operation.committed_bytes() == 6);
        CHECK(source.writes_after_exhaustion == 0);
        CHECK(storage == std::vector<std::byte>{std::byte{0x45}, std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}});
    }

    TEST_CASE("user iterator helper names do not opt into private bulk reads") {
        const std::array wire{std::byte{0x19}, std::byte{0x01}, std::byte{0x02}, std::byte{0x43}, std::byte{7}, std::byte{8}, std::byte{9}};
        named_helper_source    source(wire);
        auto                   decoder = ct::make_resumable_decoder(source);
        std::uint64_t          number{};
        std::vector<std::byte> payload;
        auto                   operation = decoder(number, payload);
        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(number == 258);
        CHECK(payload == std::vector<std::byte>{std::byte{7}, std::byte{8}, std::byte{9}});
        CHECK(source.unrelated_calls == 0);
    }

    TEST_CASE_TEMPLATE("byte string bulk reads cross scratch and chunk boundaries without consuming the suffix", Storage,
                       std::deque<std::deque<std::byte>>, std::deque<std::list<std::byte>>, std::list<std::list<std::byte>>,
                       std::vector<std::list<std::byte>>) {
        std::vector<std::byte> expected(300);
        for (std::size_t index = 0; index != expected.size(); ++index)
            expected[index] = static_cast<std::byte>(index);
        std::vector<std::byte> wire{std::byte{0x59}, std::byte{0x01}, std::byte{0x2c}};
        wire.insert(wire.end(), expected.begin(), expected.end());
        wire.push_back(std::byte{0xf6});

        for (const auto cut : {0U, 1U, 2U, 3U, 127U, 255U, 259U, 300U, 303U}) {
            CAPTURE(cut);
            Storage chunks;
            // A raw vector of lists may copy on reallocation. Its node
            // iterators must remain valid for the active source.
            if constexpr (std::same_as<Storage, std::vector<std::list<std::byte>>>)
                chunks.reserve(3);
            chunks.emplace_back(wire.begin(), wire.begin() + cut);
            auto                   source  = ct::make_input_source(chunks);
            auto                   decoder = ct::make_resumable_decoder(source);
            std::vector<std::byte> actual;
            auto                   operation = decoder(actual);
            chunks.emplace_back();
            chunks.emplace_back(wire.begin() + cut, wire.end());
            source.refresh();
            REQUIRE(operation.resume() == ct::resume_state::done);
            REQUIRE(operation.result());
            CHECK(actual == expected);
            CHECK(operation.committed_bytes() == wire.size() - 1);
            const auto tail = operation.remaining_input();
            REQUIRE(std::ranges::distance(tail) == 1);
            CHECK(*tail.begin() == std::byte{0xf6});
        }
    }

    TEST_CASE("node payload reads recover a grown tail before appended chunks") {
        const auto expected = node_payload();
        const auto wire     = node_payload_wire(expected);
        for (const std::size_t cut : {0U, 3U, 259U, 515U}) {
            CAPTURE(cut);
            std::list<std::list<std::byte>> chunks;
            chunks.emplace_back(wire.begin(), wire.begin() + static_cast<std::ptrdiff_t>(cut));
            auto                 source  = ct::make_input_source(chunks);
            auto                 decoder = ct::make_resumable_decoder(source);
            std::list<std::byte> actual;
            auto                 operation = decoder(actual);
            REQUIRE(operation.state() == ct::resume_state::need_input);
            CHECK(operation.committed_bytes() == (cut < 3 ? 0 : cut));

            const auto grown = cut + 31;
            chunks.back().insert(chunks.back().end(), wire.begin() + static_cast<std::ptrdiff_t>(cut),
                                 wire.begin() + static_cast<std::ptrdiff_t>(grown));
            chunks.emplace_back();
            chunks.emplace_back(wire.begin() + static_cast<std::ptrdiff_t>(grown), wire.end());
            source.refresh();
            REQUIRE(operation.resume() == ct::resume_state::done);
            REQUIRE(operation.result());
            CHECK(std::ranges::equal(actual, expected));
            CHECK(operation.committed_bytes() == 603);
            const auto tail = operation.remaining_input();
            REQUIRE(std::ranges::distance(tail) == 1);
            CHECK(*tail.begin() == std::byte{0xf6});
        }
    }

    TEST_CASE("node bulk bookmarks recover after owning descriptors relocate") {
        const auto                      expected = node_payload();
        const auto                      wire     = node_payload_wire(expected);
        std::vector<movable_bulk_chunk> chunks;
        chunks.reserve(1);
        chunks.emplace_back(wire.begin(), wire.begin() + 259);
        auto                 source  = ct::make_input_source(chunks);
        auto                 decoder = ct::make_resumable_decoder(source);
        std::list<std::byte> actual;
        auto                 operation = decoder(actual);
        REQUIRE(operation.state() == ct::resume_state::need_input);
        REQUIRE(operation.committed_bytes() == 259);

        // Move the exhausted list to a new descriptor address without copying
        // its nodes. An unchanged refresh must not reuse its old end iterator.
        const auto *old_data = chunks.data();
        chunks.reserve(chunks.capacity() + 2);
        REQUIRE(chunks.data() != old_data);
        source.refresh();
        REQUIRE(operation.resume() == ct::resume_state::need_input);
        CHECK(operation.committed_bytes() == 259);
        CHECK(std::ranges::equal(actual, std::span{expected}.first(256)));

        chunks.front().bytes.insert(chunks.front().bytes.end(), wire.begin() + 259, wire.begin() + 290);
        chunks.emplace_back();
        chunks.emplace_back(wire.begin() + 290, wire.end());
        source.refresh();
        REQUIRE(operation.resume() == ct::resume_state::done);
        REQUIRE(operation.result());
        CHECK(operation.committed_bytes() == 603);
        CHECK(std::ranges::equal(actual, expected));
        const auto tail = operation.remaining_input();
        REQUIRE(std::ranges::distance(tail) == 1);
        CHECK(*tail.begin() == std::byte{0xf6});
    }

    TEST_CASE("node bulk reads traverse once and do not probe a boundary per byte") {
        const auto                 expected = node_payload();
        std::array<std::size_t, 2> boundary_calls{};
        for (const std::size_t length : {1U, 600U}) {
            node_read_counts              counts;
            std::list<counted_node_chunk> chunks{{{expected.begin(), expected.begin() + static_cast<std::ptrdiff_t>(length)}, &counts}};
            auto                          source   = ct::make_input_source(chunks);
            auto                          position = source.begin();
            counts                                 = {};
            std::vector<std::byte> actual(length);
            REQUIRE(position.read_bytes(actual) == length);
            CHECK(position.offset() == length);
            CHECK(counts.increments == length);
            CHECK(counts.dereferences == length);
            CHECK(std::ranges::equal(actual, std::span{expected}.first(length)));
            boundary_calls[length == 1 ? 0 : 1] = counts.ends;

            // An exhausted unchanged node chunk does not revisit its last byte.
            REQUIRE(position.read_bytes(actual) == 0);
            CHECK(counts.increments == length);
            CHECK(counts.dereferences == length);
        }
        CHECK(boundary_calls[0] != 0);
        CHECK(boundary_calls[1] == boundary_calls[0]);
    }

    TEST_CASE("node payload read failures preserve the committed prefix") {
        const auto                    expected = node_payload();
        const auto                    wire     = node_payload_wire(expected);
        node_read_counts              counts;
        std::list<counted_node_chunk> chunks{{{wire.begin(), wire.begin() + 211}, &counts}};
        auto                          source  = ct::make_input_source(chunks);
        auto                          decoder = ct::make_resumable_decoder(source);
        std::list<std::byte>          actual;
        auto                          operation = decoder(actual);
        REQUIRE(operation.state() == ct::resume_state::need_input);
        REQUIRE(operation.committed_bytes() == 211);
        REQUIRE(std::ranges::equal(actual, std::span{expected}.first(208)));

        // Establish progress through public admission, then fail inside a
        // later read without requiring any particular internal batch size.
        chunks.push_back({{wire.begin() + 211, wire.end()}, &counts});
        source.refresh();
        counts.throw_at = counts.dereferences + 44;
        REQUIRE(operation.resume() == ct::resume_state::done);
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == ct::status_code::out_of_memory);
        REQUIRE(actual.size() >= 208);
        REQUIRE(actual.size() < expected.size());
        CHECK(std::ranges::equal(actual, std::span{expected}.first(actual.size())));
        CHECK(operation.committed_bytes() == 3 + actual.size());
        CHECK(source.committed_position().offset() == 3 + actual.size());
        counts.throw_at      = std::numeric_limits<std::size_t>::max();
        const auto remaining = operation.remaining_input();
        CHECK(*remaining.begin() == expected[actual.size()]);
        CHECK(std::ranges::distance(remaining) == wire.size() - 3 - actual.size());
    }

    TEST_CASE("byte string output commits each available prefix across deque growth") {
        std::vector<std::byte> value(300);
        for (std::size_t index = 0; index != value.size(); ++index)
            value[index] = static_cast<std::byte>(index);
        std::vector<std::byte> expected{std::byte{0x59}, std::byte{0x01}, std::byte{0x2c}};
        expected.insert(expected.end(), value.begin(), value.end());

        for (const auto cut : {0U, 1U, 2U, 3U, 127U, 255U, 259U, 300U}) {
            CAPTURE(cut);
            std::deque<std::deque<std::byte>> chunks{std::deque<std::byte>(cut, std::byte{0xcc})};
            auto                              encoder   = ct::make_resumable_encoder(chunks);
            auto                              operation = encoder(value);
            REQUIRE(operation.state() == ct::resume_state::need_output);
            CHECK(operation.committed_bytes() == (cut < 3 ? 0 : cut));

            chunks.emplace_back();
            chunks.emplace_back(expected.size() - cut, std::byte{0xcc});
            REQUIRE(operation.resume() == ct::resume_state::done);
            REQUIRE(operation.result());
            CHECK(operation.committed_bytes() == expected.size());
            std::vector<std::byte> actual;
            for (const auto &chunk : chunks)
                actual.insert(actual.end(), chunk.begin(), chunk.end());
            CHECK(actual == expected);
        }
    }

    TEST_CASE("indexed byte chunks retain a split uint64 payload and its suffix") {
        const std::array        wire{std::byte{0x1b}, std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
                                     std::byte{5},    std::byte{6}, std::byte{7}, std::byte{8}, std::byte{0xf6}};
        constexpr std::uint64_t expected = 0x0102030405060708ULL;

        for (std::size_t cut = 0; cut != wire.size(); ++cut) {
            CAPTURE(cut);
            std::deque<std::deque<std::byte>> chunks(1);
            chunks.back().insert(chunks.back().end(), wire.begin(), wire.begin() + static_cast<std::ptrdiff_t>(cut));
            auto          source  = ct::make_input_source(chunks);
            auto          decoder = ct::make_resumable_decoder(source);
            std::uint64_t value{};
            auto          operation = decoder(value);

            if (cut < 9) {
                REQUIRE(operation.state() == ct::resume_state::need_input);
                CHECK(value == 0);
            }

            // Empty descriptors and a new chunk exercise both retry and native
            // iterator reconstruction at the first byte of the remaining payload.
            chunks.emplace_back();
            chunks.emplace_back(wire.begin() + static_cast<std::ptrdiff_t>(cut), wire.end());
            source.refresh();
            REQUIRE(operation.resume() == ct::resume_state::done);
            REQUIRE(operation.result());
            CHECK(*operation.result() == 9);
            CHECK(value == expected);
            CHECK(operation.committed_bytes() == 9);
            const auto remaining = operation.remaining_input();
            REQUIRE(std::ranges::distance(remaining) == 1);
            CHECK(*remaining.begin() == std::byte{0xf6});
        }
    }

    TEST_CASE("indexed output chunks overwrite tentative scalar bytes after growth") {
        const std::array        expected{std::byte{0x1b}, std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
                                         std::byte{5},    std::byte{6}, std::byte{7}, std::byte{8}};
        constexpr std::uint64_t value = 0x0102030405060708ULL;

        for (std::size_t cut = 0; cut != expected.size(); ++cut) {
            CAPTURE(cut);
            std::deque<std::deque<std::byte>> chunks{std::deque<std::byte>(cut, std::byte{0xcc})};
            auto                              source    = ct::make_output_source(chunks);
            auto                              encoder   = ct::make_resumable_encoder(source);
            auto                              operation = encoder(value);
            REQUIRE(operation.state() == ct::resume_state::need_output);
            CHECK(operation.committed_bytes() == 0);

            chunks.emplace_back();
            chunks.emplace_back(expected.size() - cut, std::byte{0xcc});
            REQUIRE(operation.resume() == ct::resume_state::done);
            REQUIRE(operation.result());
            CHECK(operation.committed_bytes() == expected.size());

            std::vector<std::byte> actual;
            for (const auto &chunk : chunks)
                actual.insert(actual.end(), chunk.begin(), chunk.end());
            CHECK(actual == std::vector<std::byte>(expected.begin(), expected.end()));
        }
    }
}
