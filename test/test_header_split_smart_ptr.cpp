// Keep the extension header first so this test catches direct include dependencies.
// clang-format off
#include <cbor_tags/extensions/smart_ptr.h>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
// clang-format on
#include <cstddef>
#include <doctest/doctest.h>
#include <memory>
#include <vector>

using namespace cbor::tags;
using namespace cbor::tags::ext::smart_ptr;

TEST_CASE("smart pointer split header is directly usable") {
    std::vector<std::byte> encoded;
    auto                   value = std::make_unique<int>(42);
    auto                   enc   = make_encoder<nullable_ptr_codec>(encoded);
    REQUIRE(enc(value));

    std::unique_ptr<int> decoded;
    auto                 dec = make_decoder<nullable_ptr_codec>(encoded);
    REQUIRE(dec(decoded));
    REQUIRE(static_cast<bool>(decoded));
    CHECK_EQ(*decoded, 42);
}

TEST_CASE("shared graph split header is directly usable") {
    std::vector<std::byte>      encoded;
    auto                        value = std::make_shared<int>(42);
    auto                        enc   = make_encoder<shared_graph_codec>(encoded);
    shared_graph_encode_session encode_graph;
    REQUIRE(enc(as_shared_graph(encode_graph, value)));

    std::shared_ptr<int>        decoded;
    auto                        dec = make_decoder<shared_graph_codec>(encoded);
    shared_graph_decode_session decode_graph;
    REQUIRE(dec(as_shared_graph(decode_graph, decoded)));
    REQUIRE(static_cast<bool>(decoded));
    CHECK_EQ(*decoded, 42);
}

struct TestGraphStruct {
    int                 x;
    std::vector<double> ys;
};

TEST_CASE("shared graph split header roundtrips aggregate payloads") {
    std::vector<std::byte>    encoded;
    const std::vector<double> expected_ys{1.0, 2.0};

    auto                        value = std::make_shared<TestGraphStruct>(TestGraphStruct{.x = 1, .ys = expected_ys});
    auto                        enc   = make_encoder<shared_graph_codec>(encoded);
    shared_graph_encode_session encode_graph;

    REQUIRE(enc(as_shared_graph(encode_graph, value)));

    std::shared_ptr<TestGraphStruct> decoded;
    shared_graph_decode_session      decode_graph;
    auto                             dec = make_decoder<shared_graph_codec>(encoded);
    REQUIRE(dec(as_shared_graph(decode_graph, decoded)));
    REQUIRE(static_cast<bool>(decoded));
    CHECK_EQ(decoded->x, 1);
    CHECK_EQ(decoded->ys, expected_ys);
}
