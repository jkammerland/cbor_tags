#include <array>
#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
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

} // namespace

TEST_SUITE("cbor_wire/resumable_bulk") {
    TEST_CASE("a short payload write suspends without probing the exhausted source again") {
        std::vector<std::byte> storage;
        bounded_write_source   source(storage);
        auto                   encoder = ct::make_resumable_encoder(source);
        const std::array       value{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};
        source.allowance = 3;
        auto operation   = encoder(value);

        REQUIRE(operation.progress() == ct::resume_state::need_output);
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

    TEST_CASE("byte string bulk reads cross scratch and chunk boundaries without consuming the suffix") {
        std::vector<std::byte> expected(300);
        for (std::size_t index = 0; index != expected.size(); ++index)
            expected[index] = static_cast<std::byte>(index);
        std::vector<std::byte> wire{std::byte{0x59}, std::byte{0x01}, std::byte{0x2c}};
        wire.insert(wire.end(), expected.begin(), expected.end());
        wire.push_back(std::byte{0xf6});

        for (const auto cut : {0U, 1U, 2U, 3U, 127U, 255U, 259U, 300U, 303U}) {
            CAPTURE(cut);
            std::deque<std::deque<std::byte>> chunks;
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
            REQUIRE(operation.progress() == ct::resume_state::need_output);
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
                REQUIRE(operation.progress() == ct::resume_state::need_input);
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
            REQUIRE(operation.progress() == ct::resume_state::need_output);
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
