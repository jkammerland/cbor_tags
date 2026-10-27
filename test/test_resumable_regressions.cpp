#include "test_util.h"

#include <array>
#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <list>
#include <memory>
#include <ranges>
#include <span>
#include <sstream>
#include <tuple>
#include <variant>
#include <vector>

using namespace cbor::tags;

namespace {

struct forwarding_record {
    int first{};
    int second{};

    template <typename Encoder> auto encode(Encoder &encoder) const { return encoder(wrap_as_array{first, second}); }
    template <typename Decoder> auto decode(Decoder &decoder) { return decoder(wrap_as_array{first, second}); }
    bool                             operator==(const forwarding_record &) const = default;
};

struct tagged_first {
    static constexpr std::uint64_t cbor_tag = 100;
    int                            value{};
};
struct tagged_second {
    static constexpr std::uint64_t cbor_tag = 101;
    int                            value{};
};

struct tagged_transcode {
    constexpr tagged_transcode() {}
    int                            value{};
    friend constexpr std::uint64_t cbor_tag(const tagged_transcode &) { return 100; }
};

template <typename Context> typename Context::task_type resumable_transcode(Context &context, tagged_transcode &value) {
    co_return co_await context(value.value);
}

struct header_custom {
    int value{};
};
template <typename Context> struct header_codec {
    resumable_task<status_code> resumable_decode(header_custom &value, major_type major, std::byte info) {
        auto &context = static_cast<Context &>(*this);
        co_return co_await context.decode_header(value.value, {major, info});
    }
};

struct custom_empty {};
template <typename Context> typename Context::task_type resumable_decode(Context &context, custom_empty &) {
    int ignored{};
    co_return co_await context(ignored);
}
struct enclosing_custom {
    custom_empty member;
};

struct batch_counts {
    std::size_t begins{};
    std::size_t reads{};
    std::size_t increments{};
};

struct single_pass_batch {
    std::vector<std::span<const std::byte>> chunks;
    batch_counts                           &counts;

    struct iterator {
        using value_type       = std::span<const std::byte>;
        using difference_type  = std::ptrdiff_t;
        using iterator_concept = std::input_iterator_tag;
        single_pass_batch *batch{};
        std::size_t        index{};
        value_type         operator*() const {
            ++batch->counts.reads;
            return batch->chunks[index];
        }
        iterator &operator++() {
            ++batch->counts.increments;
            ++index;
            return *this;
        }
        void        operator++(int) { ++*this; }
        friend bool operator==(const iterator &it, std::default_sentinel_t) { return it.index == it.batch->chunks.size(); }
    };

    iterator begin() {
        ++counts.begins;
        return {this, 0};
    }
    std::default_sentinel_t end() const { return {}; }
};
static_assert(std::ranges::input_range<single_pass_batch>);
static_assert(!std::ranges::forward_range<single_pass_batch>);

struct single_pass_output_batch {
    std::vector<std::span<std::byte>> chunks;
    batch_counts                     &counts;

    struct iterator {
        using value_type       = std::span<std::byte>;
        using difference_type  = std::ptrdiff_t;
        using iterator_concept = std::input_iterator_tag;
        single_pass_output_batch *batch{};
        std::size_t               index{};
        value_type                operator*() const {
            ++batch->counts.reads;
            return batch->chunks[index];
        }
        iterator &operator++() {
            ++batch->counts.increments;
            ++index;
            return *this;
        }
        void        operator++(int) { ++*this; }
        friend bool operator==(const iterator &it, std::default_sentinel_t) { return it.index == it.batch->chunks.size(); }
    };

    iterator begin() {
        ++counts.begins;
        return {this, 0};
    }
    std::default_sentinel_t end() const { return {}; }
};
static_assert(std::ranges::input_range<single_pass_output_batch>);
static_assert(!std::ranges::forward_range<single_pass_output_batch>);

struct tracked_output {
    std::byte         *data;
    std::weak_ptr<int> token;

    struct iterator {
        using value_type       = std::byte;
        using difference_type  = std::ptrdiff_t;
        using iterator_concept = std::forward_iterator_tag;
        std::byte           *position{};
        std::shared_ptr<int> token;

        std::byte &operator*() const { return *position; }
        iterator  &operator++() {
            ++position;
            return *this;
        }
        iterator operator++(int) {
            auto previous = *this;
            ++*this;
            return previous;
        }
        friend bool operator==(const iterator &left, const iterator &right) { return left.position == right.position; }
    };

    iterator begin() const { return {data, token.lock()}; }
    iterator end() const { return {data + 1, token.lock()}; }
};
static_assert(std::ranges::forward_range<tracked_output>);

struct byte_proxy {
    std::byte        *value{};
                      operator std::byte() const { return *value; }
    const byte_proxy &operator=(std::byte byte) const {
        *value = byte;
        return *this;
    }
};

struct proxy_bytes {
    std::byte  *data;
    std::size_t size;

    struct iterator {
        using value_type       = std::byte;
        using difference_type  = std::ptrdiff_t;
        using iterator_concept = std::forward_iterator_tag;
        std::byte *position{};
        byte_proxy operator*() const { return {position}; }
        iterator  &operator++() {
            ++position;
            return *this;
        }
        iterator operator++(int) {
            auto previous = *this;
            ++*this;
            return previous;
        }
        friend bool operator==(const iterator &, const iterator &) = default;
    };

    iterator begin() const { return {data}; }
    iterator end() const { return {data + size}; }
};
static_assert(std::ranges::forward_range<proxy_bytes>);

} // namespace

TEST_CASE("resumable forwarding customizations retain temporary wrappers") {
    const forwarding_record   source{12, -9};
    std::array<std::byte, 64> output{};
    auto                      encode  = make_resumable_encoder()(source);
    const auto                written = encode(output);
    REQUIRE(encode.result());
    forwarding_record decoded{};
    auto              decode = make_resumable_decoder()(decoded);
    for (std::size_t i = 0; i < written.bytes; ++i)
        (void)decode(std::span{output}.subspan(i, 1));
    REQUIRE(decode.result());
    CHECK(decoded == source);
}

TEST_CASE("resumable decoding preserves tag selection through optional variants") {
    using value_type = std::variant<std::optional<tagged_first>, tagged_second>;
    const value_type       source{std::in_place_index<1>, tagged_second{42}};
    std::vector<std::byte> encoded;
    REQUIRE(make_encoder(encoded)(source));
    value_type decoded;
    auto       operation = make_resumable_decoder()(decoded);
    for (const auto &byte : encoded)
        (void)operation(std::span{&byte, 1});
    REQUIRE(operation.result());
    REQUIRE_EQ(decoded.index(), 1U);
    CHECK_EQ(std::get<1>(decoded).value, 42);
}

TEST_CASE("tagged transcode hooks work in variants and optional alternatives") {
    std::vector<std::byte> encoded;
    REQUIRE(make_encoder(encoded)(static_tag<100>{}, 42));

    SUBCASE("direct") {
        tagged_transcode decoded;
        auto             operation = make_resumable_decoder()(decoded);
        for (const auto &byte : encoded)
            (void)operation(std::span{&byte, 1});
        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK_EQ(decoded.value, 42);
    }

    SUBCASE("variant") {
        std::variant<tagged_transcode, int> decoded;
        auto                                operation = make_resumable_decoder()(decoded);
        for (const auto &byte : encoded)
            (void)operation(std::span{&byte, 1});
        REQUIRE(operation.done());
        REQUIRE(operation.result());
        REQUIRE_EQ(decoded.index(), 0U);
        CHECK_EQ(std::get<0>(decoded).value, 42);
    }

    SUBCASE("optional variant alternative") {
        std::variant<std::optional<tagged_transcode>, tagged_second> decoded;
        auto                                                         operation = make_resumable_decoder()(decoded);
        for (const auto &byte : encoded)
            (void)operation(std::span{&byte, 1});
        REQUIRE(operation.done());
        REQUIRE(operation.result());
        REQUIRE_EQ(decoded.index(), 0U);
        REQUIRE(std::get<0>(decoded));
        CHECK_EQ(std::get<0>(decoded)->value, 42);
    }

    SUBCASE("tag mismatch selects a later alternative") {
        std::vector<std::byte> later;
        REQUIRE(make_encoder(later)(tagged_second{7}));
        std::variant<std::optional<tagged_transcode>, tagged_second> decoded;
        auto                                                         operation = make_resumable_decoder()(decoded);
        for (const auto &byte : later)
            (void)operation(std::span{&byte, 1});
        REQUIRE(operation.done());
        REQUIRE(operation.result());
        REQUIRE_EQ(decoded.index(), 1U);
        CHECK_EQ(std::get<1>(decoded).value, 7);
    }
}

TEST_CASE("header codecs decode direct and already-inspected items") {
    std::vector<std::byte> encoded;
    REQUIRE(make_encoder(encoded)(11, 12, std::vector<int>{13, 14}));
    header_custom                first;
    std::optional<header_custom> second;
    std::vector<header_custom>   remaining;
    auto                         operation = make_resumable_decoder<header_codec>()(first, second, remaining);
    for (const auto &byte : encoded)
        (void)operation(std::span{&byte, 1});
    REQUIRE(operation.result());
    CHECK_EQ(first.value, 11);
    REQUIRE(second);
    CHECK_EQ(second->value, 12);
    REQUIRE_EQ(remaining.size(), 2U);
    CHECK_EQ(remaining[0].value, 13);
    CHECK_EQ(remaining[1].value, 14);
}

TEST_CASE("nested resumable customizations supply container progress") {
    std::vector<std::byte> encoded;
    REQUIRE(make_encoder(encoded)(std::vector<int>{1, 2, 3}));
    std::vector<enclosing_custom> decoded;
    auto                          operation = make_resumable_decoder()(decoded);
    (void)operation(encoded);
    REQUIRE(operation.result());
    CHECK_EQ(decoded.size(), 3U);
}

TEST_CASE("resumable batch capture traverses a temporary input range once") {
    std::vector<std::byte> encoded;
    REQUIRE(make_encoder(encoded)(42, 7));
    REQUIRE(encoded.size() >= 2U);
    const std::list<std::byte>              head{encoded.front()};
    std::vector<std::span<const std::byte>> tail;
    for (std::size_t index = 1; index < encoded.size(); ++index)
        tail.emplace_back(encoded.data() + index, 1U);
    batch_counts counts;
    int          decoded{};
    auto         operation = make_resumable_decoder()(decoded);
    const auto   progress  = operation(head, single_pass_batch{tail, counts});
    REQUIRE(operation.result());
    CHECK_EQ(decoded, 42);
    CHECK_EQ(counts.begins, 1U);
    CHECK_EQ(counts.reads, tail.size());
    CHECK_EQ(counts.increments, tail.size());
    CHECK_EQ(progress.accepted_chunks, tail.size() + 1U);
    std::vector<std::byte> remaining;
    for (const auto &chunk : operation.remaining_input())
        for (const auto byte : chunk)
            remaining.push_back(byte);
    int trailing{};
    REQUIRE(make_decoder(remaining)(trailing));
    CHECK_EQ(trailing, 7);
}

TEST_CASE("resumable output captures a temporary single-pass batch once") {
    const std::vector<int>            source{12, 300, -5};
    std::array<std::byte, 32>         storage{};
    std::vector<std::span<std::byte>> chunks;
    for (auto &byte : storage)
        chunks.emplace_back(&byte, 1U);
    batch_counts counts;
    auto         operation = make_resumable_encoder()(source);
    const auto   progress  = operation(single_pass_output_batch{chunks, counts});
    REQUIRE(operation.result());
    CHECK_EQ(progress.accepted_chunks, chunks.size());
    CHECK_EQ(counts.begins, 1U);
    CHECK_EQ(counts.reads, chunks.size());
    CHECK_EQ(counts.increments, chunks.size());
    std::vector<std::byte> encoded;
    for (const auto prefix : progress.produced())
        for (const auto byte : prefix)
            encoded.push_back(byte);
    std::vector<int> decoded;
    REQUIRE(make_decoder(encoded)(decoded));
    CHECK(decoded == source);
}

TEST_CASE("resumable chunks support proxy and volatile bytes") {
    const int              source = 42;
    std::vector<std::byte> expected;
    REQUIRE(make_encoder(expected)(source));
    std::array<std::byte, 16> storage{};
    proxy_bytes               output{storage.data(), storage.size()};
    auto                      encoder  = make_resumable_encoder()(source);
    const auto                progress = encoder(output);
    REQUIRE(encoder.result());
    std::vector<std::byte> produced;
    for (const auto prefix : progress.produced())
        for (const auto byte : prefix)
            produced.push_back(byte);
    CHECK(produced == expected);

    proxy_bytes input{storage.data(), produced.size()};
    int         decoded{};
    auto        decoder = make_resumable_decoder()(decoded);
    (void)decoder(input);
    REQUIRE(decoder.result());
    CHECK_EQ(decoded, source);

    volatile std::byte            volatile_storage[16]{};
    std::span<volatile std::byte> volatile_output{volatile_storage};
    auto                          volatile_encoder  = make_resumable_encoder()(source);
    const auto                    volatile_progress = volatile_encoder(volatile_output);
    REQUIRE(volatile_encoder.result());
    CHECK_EQ(volatile_progress.bytes, expected.size());
    int  volatile_decoded{};
    auto volatile_decoder = make_resumable_decoder()(volatile_decoded);
    (void)volatile_decoder(volatile_output.first(volatile_progress.bytes));
    REQUIRE(volatile_decoder.result());
    CHECK_EQ(volatile_decoded, source);
}

TEST_CASE("empty feeds do not mean end of input") {
    const std::array<std::byte, 0> empty{};
    int                            value{};
    auto                           operation = make_resumable_decoder()(value);
    auto                           progress  = operation(empty, empty);
    CHECK(progress.state == resumable_state::need_input);
    CHECK_EQ(progress.bytes, 0U);
    CHECK_EQ(progress.accepted_chunks, 2U);
    CHECK_FALSE(operation.done());
    progress = operation();
    CHECK(progress.state == resumable_state::need_input);
    CHECK_EQ(progress.accepted_chunks, 0U);
    (void)operation(end_of_input);
    REQUIRE_FALSE(operation.result());
    CHECK(operation.result().error() == status_code::incomplete);

    struct empty_value {
    } zero_width;
    auto zero = make_resumable_decoder()(zero_width);
    CHECK(zero().state == resumable_state::done);
    REQUIRE(zero.result());
    CHECK_EQ(*zero.result(), 0U);
}

TEST_CASE("resumable range encoding consumes an unsized producer once") {
    const std::vector<int> source{1, 3, 5};
    std::istringstream     stream{"1 3 5"};
    auto                   input     = as_array_range(std::ranges::istream_view<int>{stream});
    auto                   operation = make_resumable_encoder()(std::move(input));
    std::deque<std::byte>  output(32);
    const auto             progress = operation(output);
    REQUIRE(operation.result());
    std::vector<std::byte> encoded;
    for (const auto prefix : progress.produced())
        for (const auto byte : prefix)
            encoded.push_back(byte);
    std::vector<int> decoded;
    REQUIRE(make_decoder(encoded)(decoded));
    CHECK(decoded == source);
}

TEST_CASE("reusing unused output begins the new produced prefix at its cursor") {
    std::array<std::byte, 16> output{};
    auto                      first          = make_resumable_encoder()(1);
    const auto                first_progress = first(output);
    REQUIRE(first.result());
    auto       second          = make_resumable_encoder()(2);
    const auto second_progress = second(first_progress.unused_output());
    REQUIRE(second.result());
    CHECK_EQ(second_progress.bytes, 1U);
    std::size_t prefix_size{};
    for (const auto prefix : second_progress.produced())
        for ([[maybe_unused]] const auto byte : prefix)
            ++prefix_size;
    CHECK_EQ(prefix_size, 1U);
    int  one{};
    int  two{};
    auto decoder = make_decoder(output);
    REQUIRE(decoder(one, two));
    CHECK_EQ(one, 1);
    CHECK_EQ(two, 2);
}

TEST_CASE("resumable encoder owns temporary array wrapper values") {
    std::array<std::byte, 16> output{};
    auto                      operation = make_resumable_encoder()(wrap_as_array{3, 4});
    const auto                progress  = operation(output);
    REQUIRE(operation.result());
    int  first{};
    int  second{};
    auto decode = make_decoder(output);
    REQUIRE(decode(wrap_as_array{first, second}));
    CHECK_EQ(first, 3);
    CHECK_EQ(second, 4);
    CHECK_EQ(progress.bytes, *operation.result());
}

TEST_CASE("output generators release acknowledged buffer descriptors") {
    std::vector<int>       source(128, 7);
    auto                   operation = make_resumable_encoder().chunks(source);
    std::vector<std::byte> encoded;
    std::byte              buffer{};
    std::size_t            feeds{};
    while (!operation.done() && feeds < 512U) {
        auto                     token    = std::make_shared<int>(0);
        const std::weak_ptr<int> observer = token;
        tracked_output           chunk{&buffer, token};
        const auto               progress = operation(chunk);
        token.reset();
        REQUIRE(progress.state == resumable_state::yielded);
        REQUIRE_FALSE(observer.expired());

        auto current = operation.next();
        REQUIRE(current.await_ready());
        REQUIRE(current.await_resume());
        for (const auto byte : operation.value())
            encoded.push_back(byte);

        auto       acknowledge = operation.next();
        const bool ready       = acknowledge.await_ready();
        CHECK(observer.expired());
        if (ready)
            CHECK_FALSE(acknowledge.await_resume());
        ++feeds;
    }
    REQUIRE(operation.done());
    REQUIRE(operation.result());
    std::vector<int> decoded;
    REQUIRE(make_decoder(encoded)(decoded));
    CHECK(decoded == source);
}
