#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/smart_ptr.h>
#include <cstddef>
#include <doctest/doctest.h>
#include <memory>
#include <vector>

TEST_CASE("smart pointer split header is directly usable") {
    std::vector<std::byte> encoded;
    auto                   value = std::make_unique<int>(42);
    auto                   enc   = cbor::tags::make_encoder<cbor::tags::ext::smart_ptr::nullable_ptr_codec>(encoded);
    REQUIRE(enc(value));

    std::unique_ptr<int> decoded;
    auto                 dec = cbor::tags::make_decoder<cbor::tags::ext::smart_ptr::nullable_ptr_codec>(encoded);
    REQUIRE(dec(decoded));
    REQUIRE(decoded);
    CHECK_EQ(*decoded, 42);
}

TEST_CASE("shared graph split header is directly usable") {
    std::vector<std::byte> encoded;
    auto                   value = std::make_shared<int>(42);
    auto                   enc   = cbor::tags::make_encoder<cbor::tags::ext::smart_ptr::shared_graph_codec>(encoded);
    cbor::tags::ext::smart_ptr::shared_graph_encode_session encode_graph;
    REQUIRE(enc(cbor::tags::ext::smart_ptr::as_shared_graph(encode_graph, value)));

    std::shared_ptr<int> decoded;
    auto                 dec = cbor::tags::make_decoder<cbor::tags::ext::smart_ptr::shared_graph_codec>(encoded);
    cbor::tags::ext::smart_ptr::shared_graph_decode_session decode_graph;
    REQUIRE(dec(cbor::tags::ext::smart_ptr::as_shared_graph(decode_graph, decoded)));
    REQUIRE(decoded);
    CHECK_EQ(*decoded, 42);
}
