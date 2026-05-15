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
