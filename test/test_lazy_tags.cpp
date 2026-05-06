#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <ranges>
#include <string>
#include <vector>

using namespace cbor::tags;

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

    auto dec = it->make_decoder();
    decoded.clear();
    CHECK(dec(decoded));
    CHECK_EQ(decoded, "abc");
}

TEST_CASE("lazy tag scanner exposes contiguous payload spans") {
    auto buffer = to_bytes("d864182a");
    auto view   = find_tags<100>(buffer);
    auto it     = view.begin();

    REQUIRE(it != view.end());
    CHECK_EQ(to_hex(it->payload_span()), "182a");
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

TEST_CASE("lazy tag scanner reports truncated matching payloads") {
    auto buffer = to_bytes("d86418");
    auto view   = find_tags<100>(buffer);
    auto it     = view.begin();

    CHECK(it == view.end());
    CHECK(view.failed());
    CHECK_EQ(view.status(), status_code::incomplete);
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
