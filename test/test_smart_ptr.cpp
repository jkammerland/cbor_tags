#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/smart_ptr.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace cbor::tags;
using namespace cbor::tags::ext::smart_ptr;

namespace smart_ptr_test {

template <typename Enc, typename T>
concept CanEncode = requires(Enc &enc, const T &value) { enc.encode(value); };

template <typename Dec, typename T>
concept CanDecode = requires(Dec &dec, T &value, major_type major, std::byte additional_info) {
    { dec.decode(value, major, additional_info) } -> std::same_as<status_code>;
};

struct nullable_holder {
    std::unique_ptr<std::uint64_t> count;
    std::shared_ptr<std::string>   name;
};

struct graph_link {
    std::shared_ptr<graph_link> next;
};

template <typename T> std::vector<std::byte> encode_nullable_ptr(const T &value) {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder<nullable_ptr_codec>(buffer);
    REQUIRE(enc(value));
    return buffer;
}

} // namespace smart_ptr_test

TEST_CASE("nullable pointer codec is explicit opt in") {
    using pointer_type      = std::unique_ptr<std::uint64_t>;
    using const_pointer     = std::unique_ptr<const std::uint64_t>;
    using void_pointer      = std::shared_ptr<void>;
    using default_encoder   = decltype(make_encoder(std::declval<std::vector<std::byte> &>()));
    using extension_encoder = decltype(make_encoder<nullable_ptr_codec>(std::declval<std::vector<std::byte> &>()));
    using default_decoder   = decltype(make_decoder(std::declval<std::vector<std::byte> &>()));
    using extension_decoder = decltype(make_decoder<nullable_ptr_codec>(std::declval<std::vector<std::byte> &>()));

    static_assert(!smart_ptr_test::CanEncode<default_encoder, pointer_type>);
    static_assert(smart_ptr_test::CanEncode<extension_encoder, pointer_type>);
    static_assert(!smart_ptr_test::CanDecode<default_decoder, pointer_type>);
    static_assert(smart_ptr_test::CanDecode<extension_decoder, pointer_type>);

    static_assert(!smart_ptr_test::CanEncode<extension_encoder, const_pointer>);
    static_assert(!smart_ptr_test::CanDecode<extension_decoder, const_pointer>);
    static_assert(!smart_ptr_test::CanEncode<extension_encoder, void_pointer>);
    static_assert(!smart_ptr_test::CanDecode<extension_decoder, void_pointer>);
}

TEST_CASE("nullable pointer codec roundtrips unique_ptr null and value") {
    {
        const std::unique_ptr<std::uint64_t> value;
        const auto                           encoded = smart_ptr_test::encode_nullable_ptr(value);
        CHECK_EQ(to_hex(encoded), "f6");

        std::unique_ptr<std::uint64_t> decoded = std::make_unique<std::uint64_t>(7U);
        auto                           dec     = make_decoder<nullable_ptr_codec>(encoded);
        REQUIRE(dec(decoded));
        CHECK_FALSE(static_cast<bool>(decoded));
    }

    {
        const auto encoded = smart_ptr_test::encode_nullable_ptr(std::make_unique<std::uint64_t>(42U));
        CHECK_EQ(to_hex(encoded), "182a");

        std::unique_ptr<std::uint64_t> decoded;
        auto                           dec = make_decoder<nullable_ptr_codec>(encoded);
        REQUIRE(dec(decoded));
        REQUIRE(static_cast<bool>(decoded));
        CHECK_EQ(*decoded, 42U);
    }
}

TEST_CASE("shared graph codec requires explicit graph wrappers for shared_ptr") {
    auto shared = std::make_shared<std::uint64_t>(42U);

    {
        std::vector<std::byte> buffer;
        auto                   enc    = make_encoder<shared_graph_codec>(buffer);
        const auto             result = enc(shared);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
    }

    {
        const auto                     bytes = to_bytes("f6");
        std::shared_ptr<std::uint64_t> decoded;
        auto                           dec    = make_decoder<shared_graph_codec>(bytes);
        const auto                     result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
    }
}

TEST_CASE("shared graph codec preserves shared_ptr identity across multiple roots") {
    auto shared = std::make_shared<std::uint64_t>(42U);

    std::vector<std::byte>         buffer;
    auto                           enc = make_encoder<shared_graph_codec>(buffer);
    shared_graph_encode_session    encode_graph;
    std::shared_ptr<std::uint64_t> first  = shared;
    std::shared_ptr<std::uint64_t> second = shared;

    REQUIRE(enc(as_shared_graph(encode_graph, first)));
    REQUIRE(enc(as_shared_graph(encode_graph, second)));
    CHECK_EQ(to_hex(buffer), "830001182a820101");

    auto                           dec = make_decoder<shared_graph_codec>(buffer);
    shared_graph_decode_session    decode_graph;
    std::shared_ptr<std::uint64_t> decoded_first;
    std::shared_ptr<std::uint64_t> decoded_second;

    REQUIRE(dec(as_shared_graph(decode_graph, decoded_first)));
    REQUIRE(dec(as_shared_graph(decode_graph, decoded_second)));
    REQUIRE(static_cast<bool>(decoded_first));
    REQUIRE(static_cast<bool>(decoded_second));
    CHECK_EQ(*decoded_first, 42U);
    CHECK(decoded_first.get() == decoded_second.get());
}

TEST_CASE("shared graph codec reset starts an independent graph") {
    auto shared = std::make_shared<std::uint64_t>(42U);

    std::vector<std::byte>      buffer;
    auto                        enc = make_encoder<shared_graph_codec>(buffer);
    shared_graph_encode_session encode_graph;

    REQUIRE(enc(as_shared_graph(encode_graph, shared)));
    encode_graph.reset();
    REQUIRE(enc(as_shared_graph(encode_graph, shared)));
    CHECK_EQ(to_hex(buffer), "830001182a830001182a");

    auto                           dec = make_decoder<shared_graph_codec>(buffer);
    shared_graph_decode_session    decode_graph;
    std::shared_ptr<std::uint64_t> decoded_first;
    std::shared_ptr<std::uint64_t> decoded_second;

    REQUIRE(dec(as_shared_graph(decode_graph, decoded_first)));
    decode_graph.reset();
    REQUIRE(dec(as_shared_graph(decode_graph, decoded_second)));
    REQUIRE(static_cast<bool>(decoded_first));
    REQUIRE(static_cast<bool>(decoded_second));
    CHECK_EQ(*decoded_first, 42U);
    CHECK_EQ(*decoded_second, 42U);
    CHECK(decoded_first.get() != decoded_second.get());
}

TEST_CASE("shared graph codec reports unknown and duplicate references") {
    {
        const auto                     bytes = to_bytes("820101");
        std::shared_ptr<std::uint64_t> decoded;
        auto                           dec = make_decoder<shared_graph_codec>(bytes);
        shared_graph_decode_session    decode_graph;
        const auto                     result = dec(as_shared_graph(decode_graph, decoded));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
    }

    {
        const auto                     bytes = to_bytes("8300010183000102");
        std::shared_ptr<std::uint64_t> first;
        std::shared_ptr<std::uint64_t> second;
        auto                           dec = make_decoder<shared_graph_codec>(bytes);
        shared_graph_decode_session    decode_graph;

        REQUIRE(dec(as_shared_graph(decode_graph, first)));
        const auto result = dec(as_shared_graph(decode_graph, second));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
    }
}

TEST_CASE("shared graph codec rejects cycles") {
    auto link  = std::make_shared<smart_ptr_test::graph_link>();
    link->next = link;

    std::vector<std::byte>      buffer;
    auto                        enc = make_encoder<shared_graph_codec>(buffer);
    shared_graph_encode_session encode_graph;
    const auto                  encode_result = enc(as_shared_graph(encode_graph, link));

    REQUIRE_FALSE(encode_result);
    CHECK_EQ(encode_result.error(), status_code::error);

    buffer.clear();
    auto recovered = std::make_shared<std::uint64_t>(42U);
    REQUIRE(enc(as_shared_graph(encode_graph, recovered)));
    CHECK_EQ(to_hex(buffer), "830001182a");

    const auto                                  cycle_bytes = to_bytes("830001820101");
    std::shared_ptr<smart_ptr_test::graph_link> decoded;
    auto                                        dec = make_decoder<shared_graph_codec>(cycle_bytes);
    shared_graph_decode_session                 decode_graph;
    const auto                                  decode_result = dec(as_shared_graph(decode_graph, decoded));

    REQUIRE_FALSE(decode_result);
    CHECK_EQ(decode_result.error(), status_code::error);
}

TEST_CASE("shared graph codec rejects malformed wrappers before consuming following items") {
    const auto                     bytes = to_bytes("80182a");
    std::shared_ptr<std::uint64_t> decoded;
    auto                           dec = make_decoder<shared_graph_codec>(bytes);
    shared_graph_decode_session    decode_graph;
    const auto                     result = dec(as_shared_graph(decode_graph, decoded));

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::unexpected_group_size);

    std::uint64_t following{};
    REQUIRE(dec(following));
    CHECK_EQ(following, 42U);
}

TEST_CASE("nullable pointer codec roundtrips shared_ptr null and value") {
    {
        const std::shared_ptr<std::uint64_t> value;
        const auto                           encoded = smart_ptr_test::encode_nullable_ptr(value);
        CHECK_EQ(to_hex(encoded), "f6");

        std::shared_ptr<std::uint64_t> decoded = std::make_shared<std::uint64_t>(7U);
        auto                           dec     = make_decoder<nullable_ptr_codec>(encoded);
        REQUIRE(dec(decoded));
        CHECK_FALSE(static_cast<bool>(decoded));
    }

    {
        const auto encoded = smart_ptr_test::encode_nullable_ptr(std::make_shared<std::uint64_t>(42U));
        CHECK_EQ(to_hex(encoded), "182a");

        std::shared_ptr<std::uint64_t> decoded;
        auto                           dec = make_decoder<nullable_ptr_codec>(encoded);
        REQUIRE(dec(decoded));
        REQUIRE(static_cast<bool>(decoded));
        CHECK_EQ(*decoded, 42U);
    }
}

TEST_CASE("nullable pointer codec does not preserve repeated shared_ptr identity") {
    auto shared = std::make_shared<std::uint64_t>(42U);

    std::vector<std::shared_ptr<std::uint64_t>> values{shared, shared};
    const auto                                  encoded = smart_ptr_test::encode_nullable_ptr(values);
    CHECK_EQ(to_hex(encoded), "82182a182a");

    std::vector<std::shared_ptr<std::uint64_t>> decoded;
    auto                                        dec = make_decoder<nullable_ptr_codec>(encoded);
    REQUIRE(dec(decoded));
    REQUIRE_EQ(decoded.size(), 2U);
    REQUIRE(static_cast<bool>(decoded[0]));
    REQUIRE(static_cast<bool>(decoded[1]));
    CHECK_EQ(*decoded[0], 42U);
    CHECK_EQ(*decoded[1], 42U);
    CHECK(decoded[0].get() != decoded[1].get());
}

TEST_CASE("nullable pointer codec supports aggregate fields") {
    const smart_ptr_test::nullable_holder value{std::make_unique<std::uint64_t>(42U), std::make_shared<std::string>("Ada")};
    const auto                            encoded = smart_ptr_test::encode_nullable_ptr(value);
    CHECK_EQ(to_hex(encoded), "82182a63416461");

    smart_ptr_test::nullable_holder decoded;
    auto                            dec = make_decoder<nullable_ptr_codec>(encoded);
    REQUIRE(dec(decoded));
    REQUIRE(static_cast<bool>(decoded.count));
    REQUIRE(static_cast<bool>(decoded.name));
    CHECK_EQ(*decoded.count, 42U);
    CHECK_EQ(*decoded.name, "Ada");
}

TEST_CASE("nullable pointer codec supports pointer to aggregate value") {
    const auto encoded = smart_ptr_test::encode_nullable_ptr(
        std::make_unique<smart_ptr_test::nullable_holder>(std::make_unique<std::uint64_t>(42U), std::make_shared<std::string>("Ada")));

    std::unique_ptr<smart_ptr_test::nullable_holder> decoded;
    auto                                             dec = make_decoder<nullable_ptr_codec>(encoded);
    REQUIRE(dec(decoded));
    REQUIRE(static_cast<bool>(decoded));
    REQUIRE(static_cast<bool>(decoded->count));
    REQUIRE(static_cast<bool>(decoded->name));
    CHECK_EQ(*decoded->count, 42U);
    CHECK_EQ(*decoded->name, "Ada");
}

TEST_CASE("nullable pointer codec decodes from non-contiguous input") {
    const auto                   encoded = smart_ptr_test::encode_nullable_ptr(std::make_shared<std::string>("ok"));
    std::deque<std::byte>        input(encoded.begin(), encoded.end());
    std::shared_ptr<std::string> decoded;

    auto dec = make_decoder<nullable_ptr_codec>(input);
    REQUIRE(dec(decoded));
    REQUIRE(static_cast<bool>(decoded));
    CHECK_EQ(*decoded, "ok");
}

TEST_CASE("nullable pointer codec keeps existing value on payload decode failure") {
    {
        const auto                     bytes   = to_bytes("6178");
        std::unique_ptr<std::uint64_t> decoded = std::make_unique<std::uint64_t>(7U);
        auto                           dec     = make_decoder<nullable_ptr_codec>(bytes);
        const auto                     result  = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_uint_on_buffer);
        REQUIRE(static_cast<bool>(decoded));
        CHECK_EQ(*decoded, 7U);
    }

    {
        const auto                     bytes   = to_bytes("6178");
        std::shared_ptr<std::uint64_t> decoded = std::make_shared<std::uint64_t>(7U);
        auto                           dec     = make_decoder<nullable_ptr_codec>(bytes);
        const auto                     result  = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_uint_on_buffer);
        REQUIRE(static_cast<bool>(decoded));
        CHECK_EQ(*decoded, 7U);
    }
}
