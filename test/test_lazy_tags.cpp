#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_lazy_tags.h>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <doctest/doctest.h>
#include <iterator>
#include <map>
#include <new>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

using namespace cbor::tags;

namespace {

using cbor::tags::test::detail::allocation_failure_guard;

struct accept_any_tag {
    bool operator()(std::uint64_t) const { return true; }
};

struct lazy_tag_leaf {
    std::uint64_t id{};
    std::string   name;
};

struct lazy_tag_payload {
    lazy_tag_leaf              leaf;
    std::vector<int>           values;
    std::map<std::string, int> lookup;
};

std::vector<std::byte> make_deeply_nested_tag(std::size_t depth) {
    std::vector<std::byte> buffer;
    buffer.reserve(depth + 4);
    for (std::size_t index = 0; index < depth; ++index) {
        buffer.push_back(std::byte{0x81});
    }
    buffer.push_back(std::byte{0xD8});
    buffer.push_back(std::byte{0x64});
    buffer.push_back(std::byte{0x18});
    buffer.push_back(std::byte{0x2A});
    return buffer;
}

template <typename Buffer>
concept CanFindStaticTags = requires(Buffer &&buffer) { cbor::tags::find_tags<100>(std::forward<Buffer>(buffer)); };

template <typename Buffer>
concept CanFindRuntimeTags = requires(Buffer &&buffer) { cbor::tags::find_tags(std::forward<Buffer>(buffer), accept_any_tag{}); };

template <typename Ptr> struct lazy_tag_byte_iterator {
    Ptr p{};

    using value_type        = std::byte;
    using difference_type   = std::ptrdiff_t;
    using iterator_category = std::bidirectional_iterator_tag;
    using iterator_concept  = std::bidirectional_iterator_tag;

    decltype(auto) operator*() const { return *p; }

    lazy_tag_byte_iterator &operator++() {
        ++p;
        return *this;
    }

    lazy_tag_byte_iterator operator++(int) {
        auto copy = *this;
        ++(*this);
        return copy;
    }

    lazy_tag_byte_iterator &operator--() {
        --p;
        return *this;
    }

    lazy_tag_byte_iterator operator--(int) {
        auto copy = *this;
        --(*this);
        return copy;
    }

    friend bool operator==(lazy_tag_byte_iterator lhs, lazy_tag_byte_iterator rhs) { return lhs.p == rhs.p; }
};

struct lazy_tag_distinct_const_iterator_view : std::ranges::view_interface<lazy_tag_distinct_const_iterator_view> {
    std::byte  *first{};
    std::size_t count{};

    lazy_tag_distinct_const_iterator_view() = default;
    lazy_tag_distinct_const_iterator_view(std::byte *data, std::size_t size) : first(data), count(size) {}

    lazy_tag_byte_iterator<std::byte *>       begin() { return {first}; }
    lazy_tag_byte_iterator<std::byte *>       end() { return {first + count}; }
    lazy_tag_byte_iterator<const std::byte *> begin() const { return {first}; }
    lazy_tag_byte_iterator<const std::byte *> end() const { return {first + count}; }
};

} // namespace

void *operator new(std::size_t size) {
    if (cbor::tags::test::detail::fail_test_allocations) {
        throw std::bad_alloc{};
    }
    if (void *ptr = std::malloc(size == 0 ? 1 : size)) {
        return ptr;
    }
    throw std::bad_alloc{};
}

void *operator new[](std::size_t size) {
    if (cbor::tags::test::detail::fail_test_allocations) {
        throw std::bad_alloc{};
    }
    if (void *ptr = std::malloc(size == 0 ? 1 : size)) {
        return ptr;
    }
    throw std::bad_alloc{};
}

void operator delete(void *ptr) noexcept { std::free(ptr); }
void operator delete[](void *ptr) noexcept { std::free(ptr); }
void operator delete(void *ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete[](void *ptr, std::size_t) noexcept { std::free(ptr); }

static_assert(CanFindStaticTags<std::vector<std::byte> &>);
static_assert(CanFindStaticTags<const std::vector<std::byte> &>);
static_assert(CanFindStaticTags<std::span<const std::byte> &>);
static_assert(!CanFindStaticTags<std::vector<std::byte>>);
static_assert(!CanFindStaticTags<std::span<const std::byte>>);
static_assert(!CanFindRuntimeTags<std::vector<std::byte>>);
static_assert(!CanFindStaticTags<std::ranges::owning_view<std::vector<std::byte>> &>);
static_assert(std::ranges::view<lazy_tag_distinct_const_iterator_view>);
static_assert(CborInputBuffer<lazy_tag_distinct_const_iterator_view>);
static_assert(CanFindStaticTags<lazy_tag_distinct_const_iterator_view &>);

using lazy_tag_byte_vector        = std::vector<std::byte>;
using lazy_tag_byte_subrange      = std::ranges::subrange<lazy_tag_byte_vector::const_iterator>;
using lazy_tag_mutable_span_match = cbor::tags::tag_match<std::span<std::byte>>;
using lazy_tag_mutable_payload    = decltype(std::declval<lazy_tag_mutable_span_match &>().payload_range());
static_assert(CanFindStaticTags<lazy_tag_byte_subrange &>);
static_assert(!CanFindStaticTags<lazy_tag_byte_subrange>);
static_assert(!std::is_aggregate_v<lazy_tag_mutable_span_match>);
static_assert(!std::assignable_from<decltype(*std::declval<lazy_tag_mutable_span_match &>().payload_range().begin()), std::byte>);
static_assert(!std::assignable_from<decltype(*std::declval<lazy_tag_mutable_payload &>().range.begin()), std::byte>);

namespace {

[[nodiscard]] auto make_static_tags_from_local_span(const lazy_tag_byte_vector &buffer) {
    std::span<const std::byte> span{buffer.data(), buffer.size()};
    return cbor::tags::find_tags<100>(span);
}

[[nodiscard]] auto make_runtime_tags_from_local_subrange(const lazy_tag_byte_vector &buffer) {
    lazy_tag_byte_subrange subrange{buffer.begin(), buffer.end()};
    return cbor::tags::find_tags(subrange, [](std::uint64_t tag) { return tag == 100; });
}

} // namespace

TEST_CASE("lazy tag scanner keeps local span descriptors alive") {
    auto buffer = to_bytes("d864182a");
    auto view   = make_static_tags_from_local_span(buffer);
    auto it     = view.begin();

    REQUIRE(it != view.end());
    CHECK_EQ(it->tag(), 100);
    CHECK_EQ(to_hex(it->payload_span()), "182a");

    ++it;
    CHECK(it == view.end());
    CHECK_EQ(view.status(), status_code::success);
}

TEST_CASE("lazy tag scanner keeps local subrange descriptors alive") {
    auto buffer = to_bytes("d864182a");
    auto view   = make_runtime_tags_from_local_subrange(buffer);
    auto it     = view.begin();

    REQUIRE(it != view.end());
    CHECK_EQ(it->tag(), 100);
    CHECK_EQ(to_hex(it->payload_range()), "182a");

    int decoded{};
    CHECK(it->decode(decoded));
    CHECK_EQ(decoded, 42);

    ++it;
    CHECK(it == view.end());
    CHECK_EQ(view.status(), status_code::success);
}

TEST_CASE("lazy tag scanner view construction does not copy buffer contents") {
    auto buffer = to_bytes("d864182a");
    bool threw  = false;

    {
        allocation_failure_guard guard;
        try {
            auto vector_view = cbor::tags::find_tags<100>(buffer);

            std::span<const std::byte> span{buffer.data(), buffer.size()};
            auto                       span_view = cbor::tags::find_tags<100>(span);

            lazy_tag_byte_subrange subrange{buffer.begin(), buffer.end()};
            auto                   subrange_view = cbor::tags::find_tags<100>(subrange);

            (void)vector_view;
            (void)span_view;
            (void)subrange_view;
        } catch (...) { threw = true; }
    }

    CHECK(!threw);
}

TEST_CASE("lazy tag scanner finds matching tags in nested arrays and maps") {
    auto buffer = to_bytes("82d864182aa101d8c863616263");

    auto                     view = find_tags<100, 200>(buffer);
    std::vector<uint64_t>    tags;
    std::vector<std::string> payloads;
    for (const auto &match : view) {
        tags.push_back(match.tag());
        payloads.push_back(to_hex(match.payload_range()));
    }

    CHECK_EQ(tags, (std::vector<std::uint64_t>{100, 200}));
    CHECK_EQ(payloads, (std::vector<std::string>{"182a", "63616263"}));
    CHECK_EQ(view.status(), status_code::success);
}

TEST_CASE("lazy tag scanner supports runtime predicates and decodes only matching payloads") {
    auto buffer = to_bytes("82d864182aa101d8c863616263");

    auto view = find_tags(buffer, [](std::uint64_t tag) { return tag > 150; });
    auto it   = view.begin();
    REQUIRE(it != view.end());
    CHECK_EQ(it->tag(), 200);

    std::string decoded;
    CHECK(it->decode(decoded));
    CHECK_EQ(decoded, "abc");

    auto dec                    = it->make_decoder();
    using match_decode_result   = decltype(it->decode(decoded));
    using decoder_call_result   = decltype(dec(decoded));
    using decoder_decode_result = decltype(dec.decode(decoded));
    static_assert(std::same_as<match_decode_result, decoder_call_result>);
    static_assert(std::same_as<decoder_call_result, decoder_decode_result>);

    decoded.clear();
    CHECK(dec.decode(decoded));
    CHECK_EQ(decoded, "abc");
}

TEST_CASE("lazy tag scanner exposes contiguous payload spans") {
    auto buffer = to_bytes("d864182a");
    auto view   = find_tags<100>(buffer);
    auto it     = view.begin();

    REQUIRE(it != view.end());
    CHECK_EQ(to_hex(it->payload_span()), "182a");
}

TEST_CASE("lazy tag scanner accepts const lvalue buffers") {
    const auto buffer = to_bytes("d864182a");
    auto       view   = find_tags<100>(buffer);
    auto       it     = view.begin();

    REQUIRE(it != view.end());
    CHECK_EQ(it->tag(), 100);
    CHECK_EQ(to_hex(it->payload_span()), "182a");
}

TEST_CASE("lazy tag scanner supports views with distinct const and mutable iterators") {
    auto                                  buffer = to_bytes("d864182a");
    lazy_tag_distinct_const_iterator_view input{buffer.data(), buffer.size()};

    auto view = find_tags<100>(input);
    auto it   = view.begin();

    REQUIRE(it != view.end());
    CHECK_EQ(it->tag(), 100);
    CHECK_EQ(to_hex(it->payload_range()), "182a");

    int decoded{};
    REQUIRE(it->decode(decoded));
    CHECK_EQ(decoded, 42);

    ++it;
    CHECK(it == view.end());
    CHECK_EQ(view.status(), status_code::success);
}

TEST_CASE("lazy tag scanner supports non-contiguous buffers") {
    auto                  vector_buffer = to_bytes("82d864182aa101d8c863616263");
    std::deque<std::byte> buffer(vector_buffer.begin(), vector_buffer.end());
    auto                  view = find_tags<200>(buffer);
    auto                  it   = view.begin();

    REQUIRE(it != view.end());
    CHECK_EQ(it->tag(), 200);
    CHECK_EQ(to_hex(it->payload_range()), "63616263");

    std::string decoded;
    CHECK(it->decode(decoded));
    CHECK_EQ(decoded, "abc");
}

TEST_CASE("lazy tag scanner finds tags inside valid indefinite containers") {
    auto buffer = to_bytes("9fa101d864182abf02d8c863616263ffff");

    auto                     view = find_tags<100, 200>(buffer);
    std::vector<uint64_t>    tags;
    std::vector<std::string> payloads;
    for (const auto &match : view) {
        tags.push_back(match.tag());
        payloads.push_back(to_hex(match.payload_range()));
    }

    CHECK_EQ(tags, (std::vector<std::uint64_t>{100, 200}));
    CHECK_EQ(payloads, (std::vector<std::string>{"182a", "63616263"}));
    CHECK_EQ(view.status(), status_code::success);
}

TEST_CASE("lazy tag scanner skips large unrelated byte strings") {
    std::vector<std::byte> payload(1024, std::byte{0xAB});
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);

    REQUIRE(enc(as_array{2}, make_tag_pair(static_tag<1>{}, payload), make_tag_pair(static_tag<2>{}, 7)));

    auto view = find_tags<2>(buffer);
    auto it   = view.begin();
    REQUIRE(it != view.end());
    CHECK_EQ(it->tag(), 2);

    int decoded{};
    CHECK(it->decode(decoded));
    CHECK_EQ(decoded, 7);
    ++it;
    CHECK(it == view.end());
    CHECK_EQ(view.status(), status_code::success);
}

TEST_CASE("lazy tag scanner uses no heap allocation while scanning") {
    auto buffer = to_bytes("82820102d86401");
    auto view   = find_tags<100>(buffer);
    auto it     = decltype(view.begin()){};
    bool threw  = false;

    {
        allocation_failure_guard guard;
        try {
            it = view.begin();
            if (it != view.end()) {
                ++it;
            }
        } catch (...) { threw = true; }
    }

    CHECK(!threw);
    CHECK(it == view.end());
    CHECK_EQ(view.status(), status_code::success);
}

TEST_CASE("lazy tag scanner decodes nested aggregate payloads after no-allocation scanning") {
    lazy_tag_payload       payload{.leaf = {.id = 7, .name = "alpha"}, .values = {1, 2, 3, 4}, .lookup = {{"x", 10}, {"y", 20}}};
    std::vector<std::byte> unrelated(16 * 1024, std::byte{0xAB});
    using payload_tag = tagged_object<static_tag<901>, lazy_tag_payload>;
    std::map<int, payload_tag> nested{{5, make_tag_pair(static_tag<901>{}, payload)}};

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(as_array{2}, make_tag_pair(static_tag<1000>{}, unrelated), nested));

    auto view  = find_tags<901>(buffer);
    auto it    = decltype(view.begin()){};
    bool threw = false;

    {
        allocation_failure_guard guard;
        try {
            it = view.begin();
        } catch (...) { threw = true; }
    }

    CHECK(!threw);
    REQUIRE(it != view.end());
    CHECK_EQ(it->tag(), 901);

    lazy_tag_payload decoded;
    REQUIRE(it->decode(decoded));
    CHECK_EQ(decoded.leaf.id, payload.leaf.id);
    CHECK_EQ(decoded.leaf.name, payload.leaf.name);
    CHECK_EQ(decoded.values, payload.values);
    CHECK_EQ(decoded.lookup, payload.lookup);

    ++it;
    CHECK(it == view.end());
    CHECK_EQ(view.status(), status_code::success);

    auto full_view = find_tags<901>(buffer);
    int  matches{};
    threw = false;
    {
        allocation_failure_guard guard;
        try {
            for (auto full_it = full_view.begin(); full_it != full_view.end(); ++full_it) {
                ++matches;
            }
        } catch (...) { threw = true; }
    }
    CHECK(!threw);
    CHECK_EQ(matches, 1);
    CHECK_EQ(full_view.status(), status_code::success);
}

TEST_CASE("lazy tag scanner stress scans deeply nested definite containers without heap allocation") {
    auto buffer = make_deeply_nested_tag(200);

    auto view  = find_tags<100>(buffer);
    auto it    = decltype(view.begin()){};
    bool threw = false;

    {
        allocation_failure_guard guard;
        try {
            it = view.begin();
        } catch (...) { threw = true; }
    }

    CHECK(!threw);
    REQUIRE(it != view.end());
    CHECK_EQ(it->tag(), 100);

    int decoded{};
    REQUIRE(it->decode(decoded));
    CHECK_EQ(decoded, 42);

    ++it;
    CHECK(it == view.end());
    CHECK_EQ(view.status(), status_code::success);
}

TEST_CASE("lazy tag scanner handles no-allocation depth boundary") {
    {
        auto buffer = make_deeply_nested_tag(255);
        auto view   = find_tags<100>(buffer);
        bool threw  = false;
        int  matches{};

        {
            allocation_failure_guard guard;
            try {
                for (auto it = view.begin(); it != view.end(); ++it) {
                    ++matches;
                }
            } catch (...) { threw = true; }
        }

        CHECK(!threw);
        CHECK_EQ(matches, 1);
        CHECK_EQ(view.status(), status_code::success);
    }

    {
        auto buffer = make_deeply_nested_tag(256);
        auto view   = find_tags<100>(buffer);
        bool threw  = false;

        {
            allocation_failure_guard guard;
            try {
                auto it = view.begin();
                CHECK(it == view.end());
            } catch (...) { threw = true; }
        }

        CHECK(!threw);
        CHECK(view.failed());
        CHECK_EQ(view.status(), status_code::error);
    }
}

TEST_CASE("lazy tag scanner status reflects scanning performed so far") {
    auto buffer = to_bytes("d86401ff");
    auto view   = find_tags<100>(buffer);
    auto it     = view.begin();

    REQUIRE(it != view.end());
    CHECK_EQ(it->tag(), 100);
    CHECK_EQ(view.status(), status_code::success);

    ++it;
    CHECK(it == view.end());
    CHECK_EQ(view.status(), status_code::error);
}

TEST_CASE("lazy tag scanner yields nested matches before malformed nonmatching tag tails") {
    auto buffer = to_bytes("c19fd8640118");
    auto view   = find_tags<100>(buffer);
    auto it     = view.begin();

    REQUIRE(it != view.end());
    CHECK_EQ(it->tag(), 100);
    CHECK_EQ(to_hex(it->payload_range()), "01");
    CHECK_EQ(view.status(), status_code::success);

    bool threw = false;
    {
        allocation_failure_guard guard;
        try {
            ++it;
        } catch (...) { threw = true; }
    }
    CHECK(!threw);
    CHECK(it == view.end());
    CHECK_EQ(view.status(), status_code::incomplete);
}

TEST_CASE("lazy tag scanner reports truncated matching payloads") {
    auto buffer = to_bytes("d86418");
    auto view   = find_tags<100>(buffer);
    auto it     = view.begin();

    CHECK(it == view.end());
    CHECK(view.failed());
    CHECK_EQ(view.status(), status_code::incomplete);
}

TEST_CASE("lazy tag scanner reports malformed tagged payloads") {
    {
        auto buffer = to_bytes("d8641f");
        auto view   = find_tags<100>(buffer);
        auto it     = view.begin();
        CHECK(it == view.end());
        CHECK(view.failed());
        CHECK_EQ(view.status(), status_code::error);
    }

    {
        auto buffer = to_bytes("d864df");
        auto view   = find_tags<100>(buffer);
        auto it     = view.begin();
        CHECK(it == view.end());
        CHECK(view.failed());
        CHECK_EQ(view.status(), status_code::error);
    }

    for (const auto *hex : {"d864fc", "d864fd", "d864fe"}) {
        auto buffer = to_bytes(hex);
        auto view   = find_tags<100>(buffer);
        auto it     = view.begin();
        CAPTURE(hex);
        CHECK(it == view.end());
        CHECK(view.failed());
        CHECK_EQ(view.status(), status_code::error);
    }
}

TEST_CASE("lazy tag scanner reports malformed indefinite items") {
    {
        auto buffer = to_bytes("bf01ff");
        auto view   = find_tags<100>(buffer);
        auto it     = view.begin();
        CHECK(it == view.end());
        CHECK(view.failed());
        CHECK_EQ(view.status(), status_code::error);
    }

    {
        auto buffer = to_bytes("5f5fff");
        auto view   = find_tags<100>(buffer);
        auto it     = view.begin();
        CHECK(it == view.end());
        CHECK(view.failed());
        CHECK_EQ(view.status(), status_code::error);
    }
}
