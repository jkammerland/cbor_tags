#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/smart_ptr.h>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using namespace cbor::tags;
using namespace cbor::tags::ext::smart_ptr;
namespace smart_ptr_detail = cbor::tags::ext::smart_ptr::detail;

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

struct graph_base {
    std::uint64_t value{};
};

struct graph_derived : graph_base {
#if CBOR_TAGS_HAS_BOOST_PFR_NAMES && !CBOR_TAGS_HAS_STD_REFLECTION
    // Keep the inherited aggregate path under std reflection. Boost.PFR cannot
    // reflect inherited aggregates, so the PFR backend uses an explicit codec.
  private:
    friend cbor::tags::Access;

    template <typename Encoder> constexpr auto encode(Encoder &enc) const { return enc(value); }
#endif
};

struct graph_decode_consumes_then_fails {
    std::uint64_t value{};

  private:
    friend cbor::tags::Access;

    template <typename Decoder> constexpr auto decode(Decoder &dec) {
        auto result = dec(value);
        if (!result) {
            return result;
        }
        using decode_error = cbor::tags::unexpected<status_code>;
        return expected<void, status_code>{decode_error{status_code::error}};
    }
};

struct graph_decode_nested_then_fails {
    std::shared_ptr<std::uint64_t>   child;
    graph_decode_consumes_then_fails failing;
};

struct graph_shared_leaf {
    std::uint64_t value{};
};

struct graph_shared_branch {
    std::shared_ptr<graph_shared_leaf> primary;
    std::shared_ptr<graph_shared_leaf> secondary;
};

struct graph_shared_root {
    graph_shared_branch left;
    graph_shared_branch right;
};

struct graph_nested_encode_session_mismatch {
    std::shared_ptr<std::uint64_t> value;
    shared_graph_encode_session   *other{};

  private:
    friend cbor::tags::Access;

    template <typename Encoder> auto encode(Encoder &enc) const {
        auto result = enc(as_shared_graph(*other, value));
        if (!result) {
            throw std::runtime_error("nested shared graph session mismatch");
        }
        return result;
    }
};

struct graph_nested_decode_session_mismatch {
    std::shared_ptr<std::uint64_t> value;
    shared_graph_decode_session   *other{};

  private:
    friend cbor::tags::Access;

    template <typename Decoder> auto decode(Decoder &dec) { return dec(as_shared_graph(*other, value)); }
};

struct graph_reset_during_encode {
    std::uint64_t                value{};
    shared_graph_encode_session *graph{};

  private:
    friend cbor::tags::Access;

    template <typename Encoder> auto encode(Encoder &enc) const {
        graph->reset();
        return enc(value);
    }
};

inline shared_graph_decode_session *decode_reset_session{};

struct graph_reset_during_decode {
    std::uint64_t value{};

  private:
    friend cbor::tags::Access;

    template <typename Decoder> auto decode(Decoder &dec) {
        decode_reset_session->reset();
        return dec(value);
    }
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

    static_assert(!std::is_copy_constructible_v<shared_graph_encode_session>);
    static_assert(!std::is_copy_assignable_v<shared_graph_encode_session>);
    static_assert(!std::is_move_constructible_v<shared_graph_encode_session>);
    static_assert(!std::is_move_assignable_v<shared_graph_encode_session>);
    static_assert(!std::is_copy_constructible_v<shared_graph_decode_session>);
    static_assert(!std::is_copy_assignable_v<shared_graph_decode_session>);
    static_assert(!std::is_move_constructible_v<shared_graph_decode_session>);
    static_assert(!std::is_move_assignable_v<shared_graph_decode_session>);
}

TEST_CASE("nullable pointer codec roundtrips unique_ptr null and value") {
    {
        const std::unique_ptr<std::uint64_t> value;
        const auto                           encoded = smart_ptr_test::encode_nullable_ptr(value);
        CHECK_EQ(to_hex(encoded), "8100");

        std::unique_ptr<std::uint64_t> decoded = std::make_unique<std::uint64_t>(7U);
        auto                           dec     = make_decoder<nullable_ptr_codec>(encoded);
        REQUIRE(dec(decoded));
        CHECK_FALSE(static_cast<bool>(decoded));
    }

    {
        const auto encoded = smart_ptr_test::encode_nullable_ptr(std::make_unique<std::uint64_t>(42U));
        CHECK_EQ(to_hex(encoded), "8201182a");

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
    CHECK_EQ(to_hex(buffer), "d81c182ad81d00");

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

TEST_CASE("shared graph codec can use linear encode lookup") {
    auto shared = std::make_shared<std::uint64_t>(42U);

    std::vector<std::byte>      buffer;
    auto                        enc = make_encoder<shared_graph_codec>(buffer);
    shared_graph_encode_session encode_graph{shared_graph_encode_lookup::linear_scan};

    REQUIRE_EQ(encode_graph.lookup(), shared_graph_encode_lookup::linear_scan);
    encode_graph.reserve_unique(1U);
    REQUIRE(enc(as_shared_graph(encode_graph, shared)));
    REQUIRE(enc(as_shared_graph(encode_graph, shared)));
    CHECK_EQ(to_hex(buffer), "d81c182ad81d00");

    auto                           dec = make_decoder<shared_graph_codec>(buffer);
    shared_graph_decode_session    decode_graph;
    std::shared_ptr<std::uint64_t> decoded_first;
    std::shared_ptr<std::uint64_t> decoded_second;

    REQUIRE(dec(as_shared_graph(decode_graph, decoded_first)));
    REQUIRE(dec(as_shared_graph(decode_graph, decoded_second)));
    REQUIRE(static_cast<bool>(decoded_first));
    REQUIRE(static_cast<bool>(decoded_second));
    CHECK(decoded_first.get() == decoded_second.get());
}

TEST_CASE("shared graph reference shape is rejected by nullable pointer codec") {
    const auto                     bytes   = to_bytes("d81d00");
    std::shared_ptr<std::uint64_t> decoded = std::make_shared<std::uint64_t>(7U);
    auto                           dec     = make_decoder<nullable_ptr_codec>(bytes);
    const auto                     result  = dec(decoded);

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::no_match_for_array_on_buffer);
    REQUIRE(static_cast<bool>(decoded));
    CHECK_EQ(*decoded, 7U);
}

TEST_CASE("shared graph codec roundtrips null shared_ptr with nullable null marker") {
    {
        const std::shared_ptr<std::uint64_t> value;

        std::vector<std::byte>      buffer;
        auto                        enc = make_encoder<shared_graph_codec>(buffer);
        shared_graph_encode_session encode_graph;

        REQUIRE(enc(as_shared_graph(encode_graph, value)));
        CHECK_EQ(to_hex(buffer), "8100");

        auto                           dec     = make_decoder<shared_graph_codec>(buffer);
        shared_graph_decode_session    graph   = {};
        std::shared_ptr<std::uint64_t> decoded = std::make_shared<std::uint64_t>(7U);

        REQUIRE(dec(as_shared_graph(graph, decoded)));
        CHECK_FALSE(static_cast<bool>(decoded));
    }

    {
        const auto                     bytes = to_bytes("f6");
        auto                           dec   = make_decoder<shared_graph_codec>(bytes);
        shared_graph_decode_session    graph;
        std::shared_ptr<std::uint64_t> decoded = std::make_shared<std::uint64_t>(7U);
        const auto                     result  = dec(as_shared_graph(graph, decoded));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_tag_on_buffer);
        REQUIRE(static_cast<bool>(decoded));
        CHECK_EQ(*decoded, 7U);
    }
}

TEST_CASE("shared graph codec distinguishes optional null states") {
    const std::optional<std::shared_ptr<std::uint64_t>> missing;
    const std::optional<std::shared_ptr<std::uint64_t>> null_pointer{std::shared_ptr<std::uint64_t>{}};
    const std::optional<std::shared_ptr<std::uint64_t>> value{std::make_shared<std::uint64_t>(42U)};

    std::vector<std::byte>      buffer;
    auto                        enc = make_encoder<shared_graph_codec>(buffer);
    shared_graph_encode_session encode_graph;

    REQUIRE(enc(as_shared_graph(encode_graph, missing)));
    REQUIRE(enc(as_shared_graph(encode_graph, null_pointer)));
    REQUIRE(enc(as_shared_graph(encode_graph, value)));
    CHECK_EQ(to_hex(buffer), "f68100d81c182a");

    auto                                          dec = make_decoder<shared_graph_codec>(buffer);
    shared_graph_decode_session                   decode_graph;
    std::optional<std::shared_ptr<std::uint64_t>> decoded_missing = std::make_shared<std::uint64_t>(7U);
    std::optional<std::shared_ptr<std::uint64_t>> decoded_null    = std::make_shared<std::uint64_t>(7U);
    std::optional<std::shared_ptr<std::uint64_t>> decoded_value;

    REQUIRE(dec(as_shared_graph(decode_graph, decoded_missing)));
    REQUIRE(dec(as_shared_graph(decode_graph, decoded_null)));
    REQUIRE(dec(as_shared_graph(decode_graph, decoded_value)));
    CHECK_FALSE(decoded_missing.has_value());
    REQUIRE(decoded_null.has_value());
    CHECK_FALSE(static_cast<bool>(*decoded_null));
    REQUIRE(decoded_value.has_value());
    REQUIRE(static_cast<bool>(*decoded_value));
    CHECK_EQ(**decoded_value, 42U);
}

TEST_CASE("shared graph codec reset starts an independent graph") {
    auto shared = std::make_shared<std::uint64_t>(42U);

    std::vector<std::byte>      buffer;
    auto                        enc = make_encoder<shared_graph_codec>(buffer);
    shared_graph_encode_session encode_graph;

    REQUIRE(enc(as_shared_graph(encode_graph, shared)));
    encode_graph.reset();
    REQUIRE(enc(as_shared_graph(encode_graph, shared)));
    CHECK_EQ(to_hex(buffer), "d81c182ad81c182a");

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

TEST_CASE("shared graph codec preserves nested aggregate sharing") {
    auto shared = std::make_shared<smart_ptr_test::graph_shared_leaf>(42U);
    auto other  = std::make_shared<smart_ptr_test::graph_shared_leaf>(7U);

    smart_ptr_test::graph_shared_root root{
        .left  = {.primary = shared, .secondary = other},
        .right = {.primary = shared, .secondary = other},
    };

    std::vector<std::byte>      buffer;
    auto                        enc = make_encoder<shared_graph_codec>(buffer);
    shared_graph_encode_session encode_graph;

    REQUIRE(enc(as_shared_graph(encode_graph, root)));

    auto                              dec = make_decoder<shared_graph_codec>(buffer);
    shared_graph_decode_session       decode_graph;
    smart_ptr_test::graph_shared_root decoded;

    REQUIRE(dec(as_shared_graph(decode_graph, decoded)));
    REQUIRE(static_cast<bool>(decoded.left.primary));
    REQUIRE(static_cast<bool>(decoded.left.secondary));
    REQUIRE(static_cast<bool>(decoded.right.primary));
    REQUIRE(static_cast<bool>(decoded.right.secondary));
    CHECK_EQ(decoded.left.primary->value, 42U);
    CHECK_EQ(decoded.left.secondary->value, 7U);
    CHECK(decoded.left.primary.get() == decoded.right.primary.get());
    CHECK(decoded.left.secondary.get() == decoded.right.secondary.get());
    CHECK(decoded.left.primary.get() != decoded.left.secondary.get());
}

TEST_CASE("shared graph codec preserves nested aggregate sharing across roots") {
    auto shared        = std::make_shared<smart_ptr_test::graph_shared_leaf>(42U);
    auto first_unique  = std::make_shared<smart_ptr_test::graph_shared_leaf>(1U);
    auto second_unique = std::make_shared<smart_ptr_test::graph_shared_leaf>(2U);

    smart_ptr_test::graph_shared_root first{
        .left  = {.primary = shared, .secondary = first_unique},
        .right = {.primary = first_unique, .secondary = shared},
    };
    smart_ptr_test::graph_shared_root second{
        .left  = {.primary = second_unique, .secondary = shared},
        .right = {.primary = shared, .secondary = second_unique},
    };

    std::vector<std::byte>      buffer;
    auto                        enc = make_encoder<shared_graph_codec>(buffer);
    shared_graph_encode_session encode_graph;

    REQUIRE(enc(as_shared_graph(encode_graph, first)));
    REQUIRE(enc(as_shared_graph(encode_graph, second)));

    auto                              dec = make_decoder<shared_graph_codec>(buffer);
    shared_graph_decode_session       decode_graph;
    smart_ptr_test::graph_shared_root decoded_first;
    smart_ptr_test::graph_shared_root decoded_second;

    REQUIRE(dec(as_shared_graph(decode_graph, decoded_first)));
    REQUIRE(dec(as_shared_graph(decode_graph, decoded_second)));
    REQUIRE(static_cast<bool>(decoded_first.left.primary));
    REQUIRE(static_cast<bool>(decoded_first.left.secondary));
    REQUIRE(static_cast<bool>(decoded_second.left.primary));
    REQUIRE(static_cast<bool>(decoded_second.left.secondary));
    CHECK(decoded_first.left.primary.get() == decoded_first.right.secondary.get());
    CHECK(decoded_first.left.primary.get() == decoded_second.left.secondary.get());
    CHECK(decoded_first.left.primary.get() == decoded_second.right.primary.get());
    CHECK(decoded_first.left.secondary.get() == decoded_first.right.primary.get());
    CHECK(decoded_second.left.primary.get() == decoded_second.right.secondary.get());
    CHECK(decoded_first.left.primary.get() != decoded_first.left.secondary.get());
    CHECK(decoded_first.left.primary.get() != decoded_second.left.primary.get());
    CHECK_EQ(decoded_first.left.primary->value, 42U);
    CHECK_EQ(decoded_first.left.secondary->value, 1U);
    CHECK_EQ(decoded_second.left.primary->value, 2U);
}

TEST_CASE("shared graph codec decodes nested shareables after table growth") {
    auto root = std::make_shared<std::vector<std::shared_ptr<std::uint64_t>>>();
    root->reserve(130U);
    for (std::uint64_t i = 0; i < 129U; ++i) {
        root->push_back(std::make_shared<std::uint64_t>(i));
    }
    root->push_back((*root)[42U]);

    std::vector<std::byte>      buffer;
    auto                        enc = make_encoder<shared_graph_codec>(buffer);
    shared_graph_encode_session encode_graph;

    REQUIRE(enc(as_shared_graph(encode_graph, root)));

    auto                                                         dec = make_decoder<shared_graph_codec>(buffer);
    shared_graph_decode_session                                  decode_graph;
    std::shared_ptr<std::vector<std::shared_ptr<std::uint64_t>>> decoded;

    REQUIRE(dec(as_shared_graph(decode_graph, decoded)));
    REQUIRE(static_cast<bool>(decoded));
    REQUIRE_EQ(decoded->size(), 130U);
    for (std::uint64_t i = 0; i < 129U; ++i) {
        REQUIRE(static_cast<bool>((*decoded)[i]));
        CHECK_EQ(*(*decoded)[i], i);
    }
    REQUIRE(static_cast<bool>((*decoded)[129U]));
    CHECK((*decoded)[129U].get() == (*decoded)[42U].get());
}

TEST_CASE("shared graph codec rejects reset while a graph operation is active") {
    {
        std::vector<std::byte>      buffer;
        auto                        enc = make_encoder<shared_graph_codec>(buffer);
        shared_graph_encode_session graph;
        auto                        value = std::make_shared<smart_ptr_test::graph_reset_during_encode>();
        value->value                      = 42U;
        value->graph                      = &graph;

        const auto result = enc(as_shared_graph(graph, value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);

        buffer.clear();
        auto recovered = std::make_shared<std::uint64_t>(42U);
        REQUIRE(enc(as_shared_graph(graph, recovered)));
        CHECK_EQ(to_hex(buffer), "d81c182a");
    }

    {
        const auto bytes = to_bytes("d81c182a");
        auto       dec   = make_decoder<shared_graph_codec>(bytes);

        shared_graph_decode_session                                graph;
        std::shared_ptr<smart_ptr_test::graph_reset_during_decode> decoded = std::make_shared<smart_ptr_test::graph_reset_during_decode>();
        decoded->value                                                     = 7U;
        smart_ptr_test::decode_reset_session                               = &graph;

        const auto result                    = dec(as_shared_graph(graph, decoded));
        smart_ptr_test::decode_reset_session = nullptr;

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        REQUIRE(static_cast<bool>(decoded));
        CHECK_EQ(decoded->value, 7U);
    }
}

TEST_CASE("shared graph codec keeps encoded roots alive until session reset") {
    std::weak_ptr<std::uint64_t> weak;

    std::vector<std::byte>      buffer;
    auto                        enc = make_encoder<shared_graph_codec>(buffer);
    shared_graph_encode_session encode_graph;

    {
        auto shared = std::make_shared<std::uint64_t>(42U);
        weak        = shared;
        REQUIRE(enc(as_shared_graph(encode_graph, shared)));
    }

    CHECK_FALSE(weak.expired());
    encode_graph.reset();
    CHECK(weak.expired());
}

TEST_CASE("shared graph codec reports unknown references") {
    {
        const auto                     bytes = to_bytes("d81d00");
        std::shared_ptr<std::uint64_t> decoded;
        auto                           dec = make_decoder<shared_graph_codec>(bytes);
        shared_graph_decode_session    decode_graph;
        const auto                     result = dec(as_shared_graph(decode_graph, decoded));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
    }
}

TEST_CASE("shared graph codec does not register null pointers as references") {
    const auto bytes = to_bytes("8100d81d00");
    auto       dec   = make_decoder<shared_graph_codec>(bytes);

    shared_graph_decode_session    decode_graph;
    std::shared_ptr<std::uint64_t> decoded_null = std::make_shared<std::uint64_t>(7U);
    std::shared_ptr<std::uint64_t> decoded_ref  = std::make_shared<std::uint64_t>(13U);

    REQUIRE(dec(as_shared_graph(decode_graph, decoded_null)));
    CHECK_FALSE(static_cast<bool>(decoded_null));

    const auto result = dec(as_shared_graph(decode_graph, decoded_ref));
    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::error);
    REQUIRE(static_cast<bool>(decoded_ref));
    CHECK_EQ(*decoded_ref, 13U);
}

TEST_CASE("shared graph codec decodes duplicate references to the same object") {
    const auto bytes = to_bytes("d81c182ad81d00d81d00");
    auto       dec   = make_decoder<shared_graph_codec>(bytes);

    shared_graph_decode_session    decode_graph;
    std::shared_ptr<std::uint64_t> first;
    std::shared_ptr<std::uint64_t> second;
    std::shared_ptr<std::uint64_t> third;

    REQUIRE(dec(as_shared_graph(decode_graph, first)));
    REQUIRE(dec(as_shared_graph(decode_graph, second)));
    REQUIRE(dec(as_shared_graph(decode_graph, third)));
    REQUIRE(static_cast<bool>(first));
    REQUIRE(static_cast<bool>(second));
    REQUIRE(static_cast<bool>(third));
    CHECK_EQ(*first, 42U);
    CHECK(first.get() == second.get());
    CHECK(first.get() == third.get());
}

TEST_CASE("shared graph codec rejects static pointer type mismatches") {
    {
        auto derived   = std::make_shared<smart_ptr_test::graph_derived>();
        derived->value = 42U;

        std::shared_ptr<smart_ptr_test::graph_base> base = derived;
        REQUIRE(static_cast<const void *>(base.get()) == static_cast<const void *>(derived.get()));

        std::vector<std::byte>      buffer;
        auto                        enc = make_encoder<shared_graph_codec>(buffer);
        shared_graph_encode_session encode_graph;

        REQUIRE(enc(as_shared_graph(encode_graph, base)));
        const auto result = enc(as_shared_graph(encode_graph, derived));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK_EQ(to_hex(buffer), "d81c182a");
    }

    {
        const auto bytes = to_bytes("d81c182ad81d00");

        std::shared_ptr<std::uint64_t> first;
        std::shared_ptr<std::int64_t>  second;
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
    link->next.reset();

    REQUIRE_FALSE(encode_result);
    CHECK_EQ(encode_result.error(), status_code::error);

    buffer.clear();
    auto recovered = std::make_shared<std::uint64_t>(42U);
    REQUIRE(enc(as_shared_graph(encode_graph, recovered)));
    CHECK_EQ(to_hex(buffer), "d81c182a");

    const auto                                  cycle_bytes = to_bytes("d81cd81d00");
    std::shared_ptr<smart_ptr_test::graph_link> decoded;
    auto                                        dec = make_decoder<shared_graph_codec>(cycle_bytes);
    shared_graph_decode_session                 decode_graph;
    const auto                                  decode_result      = dec(as_shared_graph(decode_graph, decoded));
    const bool                                  decoded_cycle_root = static_cast<bool>(decoded);
    if (decoded) {
        decoded->next.reset();
    }

    REQUIRE_FALSE(decode_result);
    CHECK_EQ(decode_result.error(), status_code::error);
    CHECK_FALSE(decoded_cycle_root);
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

TEST_CASE("shared graph codec rejects malformed graph wrappers") {
    struct malformed_case {
        const char                  *hex;
        status_code                  expected;
        std::optional<std::uint64_t> following;
    };

    const malformed_case cases[]{
        {"8101", status_code::error, std::nullopt},                           // [1]
        {"8201182a", status_code::unexpected_group_size, 1U},                 // [1, 42]
        {"d8", status_code::incomplete, std::nullopt},                        // #6.<missing tag argument>
        {"d81c", status_code::incomplete, std::nullopt},                      // #6.28(<missing>)
        {"d81c6161", status_code::no_match_for_uint_on_buffer, std::nullopt}, // #6.28("a")
        {"d81d", status_code::incomplete, std::nullopt},                      // #6.29(<missing>)
        {"d81d6161", status_code::no_match_for_uint_on_buffer, std::nullopt}, // #6.29("a")
        {"d81d00", status_code::error, std::nullopt},                         // unknown ref 0
        {"d81e00", status_code::no_match_for_tag, 0U},                        // unknown tag 30
    };

    for (const auto &test_case : cases) {
        CAPTURE(test_case.hex);

        auto                           bytes   = to_bytes(test_case.hex);
        std::shared_ptr<std::uint64_t> decoded = std::make_shared<std::uint64_t>(7U);
        auto                           dec     = make_decoder<shared_graph_codec>(bytes);
        shared_graph_decode_session    graph;
        const auto                     result = dec(as_shared_graph(graph, decoded));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), test_case.expected);
        REQUIRE(static_cast<bool>(decoded));
        CHECK_EQ(*decoded, 7U);

        if (test_case.following.has_value()) {
            std::uint64_t following{};
            REQUIRE(dec(following));
            CHECK_EQ(following, *test_case.following);
        }
    }
}

TEST_CASE("shared graph codec rejects nested wrappers with different graph sessions") {
    {
        std::vector<std::byte>                               buffer;
        auto                                                 enc = make_encoder<shared_graph_codec>(buffer);
        shared_graph_encode_session                          graph;
        smart_ptr_test::graph_nested_encode_session_mismatch root{.value = std::make_shared<std::uint64_t>(42U), .other = &graph};

        REQUIRE(enc(as_shared_graph(graph, root)));
        CHECK_EQ(to_hex(buffer), "d81c182a");
    }

    {
        std::vector<std::byte>                               buffer;
        auto                                                 enc = make_encoder<shared_graph_codec>(buffer);
        shared_graph_encode_session                          outer;
        shared_graph_encode_session                          inner;
        smart_ptr_test::graph_nested_encode_session_mismatch root{.value = std::make_shared<std::uint64_t>(42U), .other = &inner};

        const auto result = enc(as_shared_graph(outer, root));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK(buffer.empty());
    }

    {
        const auto                                           bytes = to_bytes("d81c182a");
        auto                                                 dec   = make_decoder<shared_graph_codec>(bytes);
        shared_graph_decode_session                          graph;
        smart_ptr_test::graph_nested_decode_session_mismatch root{.value = {}, .other = &graph};

        REQUIRE(dec(as_shared_graph(graph, root)));
        REQUIRE(static_cast<bool>(root.value));
        CHECK_EQ(*root.value, 42U);
    }

    {
        const auto                                           bytes = to_bytes("d81c182a");
        auto                                                 dec   = make_decoder<shared_graph_codec>(bytes);
        shared_graph_decode_session                          outer;
        shared_graph_decode_session                          inner;
        smart_ptr_test::graph_nested_decode_session_mismatch root{.value = {}, .other = &inner};

        const auto result = dec(as_shared_graph(outer, root));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
    }
}

TEST_CASE("shared graph codec decode failure only removes the failed graph id") {
    const auto bytes = to_bytes("d81c182ad81c0dd81d00");

    std::shared_ptr<std::uint64_t>                                    first;
    std::shared_ptr<smart_ptr_test::graph_decode_consumes_then_fails> failed;
    std::shared_ptr<std::uint64_t>                                    reference;
    auto                                                              dec = make_decoder<shared_graph_codec>(bytes);
    shared_graph_decode_session                                       decode_graph;

    REQUIRE(dec(as_shared_graph(decode_graph, first)));

    const auto failed_result = dec(as_shared_graph(decode_graph, failed));
    REQUIRE_FALSE(failed_result);
    CHECK_EQ(failed_result.error(), status_code::error);

    REQUIRE(dec(as_shared_graph(decode_graph, reference)));
    REQUIRE(static_cast<bool>(first));
    REQUIRE(static_cast<bool>(reference));
    CHECK(first.get() == reference.get());
    CHECK_EQ(*reference, 42U);
}

TEST_CASE("shared graph codec rolls back nested definitions from failed roots") {
    const auto bytes = to_bytes("d81c82d81c182a0dd81d01");

    std::shared_ptr<smart_ptr_test::graph_decode_nested_then_fails> failed;
    std::shared_ptr<std::uint64_t>                                  leaked = std::make_shared<std::uint64_t>(7U);
    auto                                                            dec    = make_decoder<shared_graph_codec>(bytes);
    shared_graph_decode_session                                     graph;

    const auto failed_result = dec(as_shared_graph(graph, failed));
    REQUIRE_FALSE(failed_result);
    CHECK_EQ(failed_result.error(), status_code::error);

    const auto leaked_result = dec(as_shared_graph(graph, leaked));
    REQUIRE_FALSE(leaked_result);
    CHECK_EQ(leaked_result.error(), status_code::error);
    REQUIRE(static_cast<bool>(leaked));
    CHECK_EQ(*leaked, 7U);
}

TEST_CASE("nullable pointer codec roundtrips shared_ptr null and value") {
    {
        const std::shared_ptr<std::uint64_t> value;
        const auto                           encoded = smart_ptr_test::encode_nullable_ptr(value);
        CHECK_EQ(to_hex(encoded), "8100");

        std::shared_ptr<std::uint64_t> decoded = std::make_shared<std::uint64_t>(7U);
        auto                           dec     = make_decoder<nullable_ptr_codec>(encoded);
        REQUIRE(dec(decoded));
        CHECK_FALSE(static_cast<bool>(decoded));
    }

    {
        const auto encoded = smart_ptr_test::encode_nullable_ptr(std::make_shared<std::uint64_t>(42U));
        CHECK_EQ(to_hex(encoded), "8201182a");

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
    CHECK_EQ(to_hex(encoded), "828201182a8201182a");

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
    CHECK_EQ(to_hex(encoded), "828201182a820163416461");

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
    CHECK_EQ(to_hex(encoded), "8201828201182a820163416461");

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

TEST_CASE("shared graph codec decodes from non-contiguous input") {
    {
        const auto                     encoded = to_bytes("d81c182ad81d00");
        std::deque<std::byte>          input(encoded.begin(), encoded.end());
        auto                           dec = make_decoder<shared_graph_codec>(input);
        shared_graph_decode_session    decode_graph;
        std::shared_ptr<std::uint64_t> first;
        std::shared_ptr<std::uint64_t> second;

        REQUIRE(dec(as_shared_graph(decode_graph, first)));
        REQUIRE(dec(as_shared_graph(decode_graph, second)));
        REQUIRE(static_cast<bool>(first));
        REQUIRE(static_cast<bool>(second));
        CHECK_EQ(*first, 42U);
        CHECK(first.get() == second.get());
    }

    {
        const auto                     encoded = to_bytes("d81d00");
        std::deque<std::byte>          input(encoded.begin(), encoded.end());
        auto                           dec = make_decoder<shared_graph_codec>(input);
        shared_graph_decode_session    decode_graph;
        std::shared_ptr<std::uint64_t> decoded;
        const auto                     result = dec(as_shared_graph(decode_graph, decoded));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
    }
}

TEST_CASE("nullable pointer codec keeps existing value on payload decode failure") {
    {
        const auto                     bytes   = to_bytes("82016178");
        std::unique_ptr<std::uint64_t> decoded = std::make_unique<std::uint64_t>(7U);
        auto                           dec     = make_decoder<nullable_ptr_codec>(bytes);
        const auto                     result  = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_uint_on_buffer);
        REQUIRE(static_cast<bool>(decoded));
        CHECK_EQ(*decoded, 7U);
    }

    {
        const auto                     bytes   = to_bytes("82016178");
        std::shared_ptr<std::uint64_t> decoded = std::make_shared<std::uint64_t>(7U);
        auto                           dec     = make_decoder<nullable_ptr_codec>(bytes);
        const auto                     result  = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_uint_on_buffer);
        REQUIRE(static_cast<bool>(decoded));
        CHECK_EQ(*decoded, 7U);
    }
}

TEST_CASE("nullable pointer codec distinguishes optional null states") {
    {
        const std::optional<std::shared_ptr<std::uint64_t>> value;
        const auto                                          encoded = smart_ptr_test::encode_nullable_ptr(value);
        CHECK_EQ(to_hex(encoded), "f6");

        std::optional<std::shared_ptr<std::uint64_t>> decoded = std::make_shared<std::uint64_t>(7U);
        auto                                          dec     = make_decoder<nullable_ptr_codec>(encoded);
        REQUIRE(dec(decoded));
        CHECK_FALSE(decoded.has_value());
    }

    {
        const std::optional<std::shared_ptr<std::uint64_t>> value{std::shared_ptr<std::uint64_t>{}};
        const auto                                          encoded = smart_ptr_test::encode_nullable_ptr(value);
        CHECK_EQ(to_hex(encoded), "8100");

        std::optional<std::shared_ptr<std::uint64_t>> decoded = std::make_shared<std::uint64_t>(7U);
        auto                                          dec     = make_decoder<nullable_ptr_codec>(encoded);
        REQUIRE(dec(decoded));
        REQUIRE(decoded.has_value());
        CHECK_FALSE(static_cast<bool>(*decoded));
    }

    {
        const std::optional<std::shared_ptr<std::uint64_t>> value{std::make_shared<std::uint64_t>(42U)};
        const auto                                          encoded = smart_ptr_test::encode_nullable_ptr(value);
        CHECK_EQ(to_hex(encoded), "8201182a");

        std::optional<std::shared_ptr<std::uint64_t>> decoded;
        auto                                          dec = make_decoder<nullable_ptr_codec>(encoded);
        REQUIRE(dec(decoded));
        REQUIRE(decoded.has_value());
        REQUIRE(static_cast<bool>(*decoded));
        CHECK_EQ(**decoded, 42U);
    }
}

TEST_CASE("nullable pointer codec decodes unambiguous smart pointer variants") {
    using value_type = std::variant<std::shared_ptr<std::uint64_t>, std::string>;

    {
        value_type value{std::make_shared<std::uint64_t>(42U)};

        std::vector<std::byte> buffer;
        auto                   enc = make_encoder<nullable_ptr_codec>(buffer);
        REQUIRE(enc(value));
        CHECK_EQ(to_hex(buffer), "8201182a");

        value_type decoded{std::string{"before"}};
        auto       dec = make_decoder<nullable_ptr_codec>(buffer);
        REQUIRE(dec(decoded));
        REQUIRE(std::holds_alternative<std::shared_ptr<std::uint64_t>>(decoded));
        const auto &decoded_ptr = std::get<std::shared_ptr<std::uint64_t>>(decoded);
        REQUIRE(static_cast<bool>(decoded_ptr));
        CHECK_EQ(*decoded_ptr, 42U);
    }

    {
        value_type value{std::shared_ptr<std::uint64_t>{}};

        std::vector<std::byte> buffer;
        auto                   enc = make_encoder<nullable_ptr_codec>(buffer);
        REQUIRE(enc(value));
        CHECK_EQ(to_hex(buffer), "8100");

        value_type decoded{std::string{"before"}};
        auto       dec = make_decoder<nullable_ptr_codec>(buffer);
        REQUIRE(dec(decoded));
        REQUIRE(std::holds_alternative<std::shared_ptr<std::uint64_t>>(decoded));
        CHECK_FALSE(static_cast<bool>(std::get<std::shared_ptr<std::uint64_t>>(decoded)));
    }

    {
        value_type value{std::string{"ok"}};

        std::vector<std::byte> buffer;
        auto                   enc = make_encoder<nullable_ptr_codec>(buffer);
        REQUIRE(enc(value));
        CHECK_EQ(to_hex(buffer), "626f6b");

        value_type decoded{std::shared_ptr<std::uint64_t>{}};
        auto       dec = make_decoder<nullable_ptr_codec>(buffer);
        REQUIRE(dec(decoded));
        REQUIRE(std::holds_alternative<std::string>(decoded));
        CHECK_EQ(std::get<std::string>(decoded), "ok");
    }
}

TEST_CASE("nullable pointer variants preserve bounded alternative size errors") {
    using bounded_text = bounded_size<std::string, 1, 4>;
    using value_type   = std::variant<std::shared_ptr<std::uint64_t>, bounded_text>;

    SUBCASE("boundary value roundtrips") {
        value_type input{bounded_text{std::string{"name"}}};

        std::vector<std::byte> buffer;
        auto                   enc = make_encoder<nullable_ptr_codec>(buffer);
        REQUIRE(enc(input));

        value_type output{std::shared_ptr<std::uint64_t>{}};
        auto       dec = make_decoder<nullable_ptr_codec>(buffer);
        REQUIRE(dec(output));
        REQUIRE(std::holds_alternative<bounded_text>(output));
        CHECK_EQ(std::get<bounded_text>(output).value(), "name");
    }

    SUBCASE("oversized value reports the bound and preserves the destination") {
        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);
        REQUIRE(enc(std::string{"names"}));

        auto       original = std::make_shared<std::uint64_t>(9U);
        value_type output{original};
        auto       dec    = make_decoder<nullable_ptr_codec>(buffer);
        auto       result = dec(output);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        REQUIRE(std::holds_alternative<std::shared_ptr<std::uint64_t>>(output));
        CHECK(std::get<std::shared_ptr<std::uint64_t>>(output) == original);
    }
}

TEST_CASE("nullable pointer variants preserve malformed pointer errors") {
    using value_type = std::variant<std::shared_ptr<std::uint64_t>, std::string>;

    {
        const auto bytes = to_bytes("8102");
        value_type value{std::string{"before"}};
        auto       dec    = make_decoder<nullable_ptr_codec>(bytes);
        const auto result = dec(value);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        REQUIRE(std::holds_alternative<std::string>(value));
        CHECK_EQ(std::get<std::string>(value), "before");
    }

    {
        const auto bytes = to_bytes("80");
        value_type value{std::string{"before"}};
        auto       dec    = make_decoder<nullable_ptr_codec>(bytes);
        const auto result = dec(value);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::unexpected_group_size);
        REQUIRE(std::holds_alternative<std::string>(value));
        CHECK_EQ(std::get<std::string>(value), "before");
    }
}

TEST_CASE("shared graph codec decodes unambiguous smart pointer variants inside graph roots") {
    using value_type = std::variant<std::shared_ptr<std::uint64_t>, std::string>;

    auto       shared = std::make_shared<std::uint64_t>(42U);
    value_type first{shared};
    value_type second{shared};

    std::vector<std::byte>      buffer;
    auto                        enc = make_encoder<shared_graph_codec>(buffer);
    shared_graph_encode_session encode_graph;

    REQUIRE(enc(as_shared_graph(encode_graph, first)));
    REQUIRE(enc(as_shared_graph(encode_graph, second)));
    CHECK_EQ(to_hex(buffer), "d81c182ad81d00");

    auto                        dec = make_decoder<shared_graph_codec>(buffer);
    shared_graph_decode_session decode_graph;
    value_type                  decoded_first{std::string{"first"}};
    value_type                  decoded_second{std::string{"second"}};

    REQUIRE(dec(as_shared_graph(decode_graph, decoded_first)));
    REQUIRE(dec(as_shared_graph(decode_graph, decoded_second)));
    REQUIRE(std::holds_alternative<std::shared_ptr<std::uint64_t>>(decoded_first));
    REQUIRE(std::holds_alternative<std::shared_ptr<std::uint64_t>>(decoded_second));
    const auto &first_ptr  = std::get<std::shared_ptr<std::uint64_t>>(decoded_first);
    const auto &second_ptr = std::get<std::shared_ptr<std::uint64_t>>(decoded_second);
    REQUIRE(static_cast<bool>(first_ptr));
    REQUIRE(static_cast<bool>(second_ptr));
    CHECK_EQ(*first_ptr, 42U);
    CHECK(first_ptr.get() == second_ptr.get());
}

TEST_CASE("shared graph codec decodes vector smart pointer variants inside graph roots") {
    using vector_type = std::vector<std::shared_ptr<std::uint64_t>>;
    using value_type  = std::variant<vector_type, std::string>;

    static_assert(smart_ptr_detail::has_decodable_shared_graph_vector_v<vector_type, std::string>);
    static_assert(!smart_ptr_detail::has_decodable_nullable_pointer_v<vector_type, std::string>);

    auto       shared = std::make_shared<std::uint64_t>(42U);
    value_type value{vector_type{shared, shared}};

    std::vector<std::byte>      buffer;
    auto                        enc = make_encoder<shared_graph_codec>(buffer);
    shared_graph_encode_session encode_graph;

    REQUIRE(enc(as_shared_graph(encode_graph, value)));
    CHECK_EQ(to_hex(buffer), "82d81c182ad81d00");

    auto                        dec = make_decoder<shared_graph_codec>(buffer);
    shared_graph_decode_session decode_graph;
    value_type                  decoded{std::string{"before"}};

    REQUIRE(dec(as_shared_graph(decode_graph, decoded)));
    REQUIRE(std::holds_alternative<vector_type>(decoded));
    const auto &decoded_vector = std::get<vector_type>(decoded);
    REQUIRE_EQ(decoded_vector.size(), 2U);
    REQUIRE(static_cast<bool>(decoded_vector[0]));
    REQUIRE(static_cast<bool>(decoded_vector[1]));
    CHECK_EQ(*decoded_vector[0], 42U);
    CHECK(decoded_vector[0].get() == decoded_vector[1].get());

    value_type             text{std::string{"ok"}};
    std::vector<std::byte> text_buffer;
    auto                   text_enc = make_encoder<shared_graph_codec>(text_buffer);
    REQUIRE(text_enc(as_shared_graph(encode_graph, text)));
    CHECK_EQ(to_hex(text_buffer), "626f6b");

    auto       text_dec = make_decoder<shared_graph_codec>(text_buffer);
    value_type decoded_text{vector_type{}};
    REQUIRE(text_dec(as_shared_graph(decode_graph, decoded_text)));
    REQUIRE(std::holds_alternative<std::string>(decoded_text));
    CHECK_EQ(std::get<std::string>(decoded_text), "ok");
}

TEST_CASE("shared graph vector variants preserve malformed pointer element errors") {
    using vector_type = std::vector<std::shared_ptr<std::uint64_t>>;
    using value_type  = std::variant<vector_type, std::string>;

    {
        const auto                  bytes = to_bytes("81d81d00");
        auto                        dec   = make_decoder<shared_graph_codec>(bytes);
        shared_graph_decode_session decode_graph;
        value_type                  value{std::string{"before"}};
        const auto                  result = dec(as_shared_graph(decode_graph, value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        REQUIRE(std::holds_alternative<std::string>(value));
        CHECK_EQ(std::get<std::string>(value), "before");
    }

    {
        const auto                  bytes = to_bytes("81d81c6161");
        auto                        dec   = make_decoder<shared_graph_codec>(bytes);
        shared_graph_decode_session decode_graph;
        value_type                  value{std::string{"before"}};
        const auto                  result = dec(as_shared_graph(decode_graph, value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_uint_on_buffer);
        REQUIRE(std::holds_alternative<std::string>(value));
        CHECK_EQ(std::get<std::string>(value), "before");
    }

    {
        const auto                  bytes = to_bytes("818101");
        auto                        dec   = make_decoder<shared_graph_codec>(bytes);
        shared_graph_decode_session decode_graph;
        value_type                  value{std::string{"before"}};
        const auto                  result = dec(as_shared_graph(decode_graph, value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        REQUIRE(std::holds_alternative<std::string>(value));
        CHECK_EQ(std::get<std::string>(value), "before");
    }
}

TEST_CASE("shared graph vector variants roll back decoded ids after element failures") {
    using vector_type = std::vector<std::shared_ptr<std::uint64_t>>;
    using value_type  = std::variant<vector_type, std::string>;

    const auto                  bytes = to_bytes("82d81c182ad81d01d81d00");
    auto                        dec   = make_decoder<shared_graph_codec>(bytes);
    shared_graph_decode_session decode_graph;
    value_type                  value{std::string{"before"}};

    const auto failed_vector = dec(as_shared_graph(decode_graph, value));
    REQUIRE_FALSE(failed_vector);
    CHECK_EQ(failed_vector.error(), status_code::error);
    REQUIRE(std::holds_alternative<std::string>(value));
    CHECK_EQ(std::get<std::string>(value), "before");

    auto       leaked = std::make_shared<std::uint64_t>(7U);
    const auto ref    = dec(as_shared_graph(decode_graph, leaked));
    REQUIRE_FALSE(ref);
    CHECK_EQ(ref.error(), status_code::error);
    REQUIRE(static_cast<bool>(leaked));
    CHECK_EQ(*leaked, 7U);
}

TEST_CASE("shared graph vector variants roundtrip null pointer elements") {
    using vector_type = std::vector<std::shared_ptr<std::uint64_t>>;
    using value_type  = std::variant<vector_type, std::string>;

    value_type value{vector_type{std::shared_ptr<std::uint64_t>{}}};

    std::vector<std::byte>      buffer;
    auto                        enc = make_encoder<shared_graph_codec>(buffer);
    shared_graph_encode_session encode_graph;

    REQUIRE(enc(as_shared_graph(encode_graph, value)));
    CHECK_EQ(to_hex(buffer), "818100");

    auto                        dec = make_decoder<shared_graph_codec>(buffer);
    shared_graph_decode_session decode_graph;
    value_type                  decoded{std::string{"before"}};

    REQUIRE(dec(as_shared_graph(decode_graph, decoded)));
    REQUIRE(std::holds_alternative<vector_type>(decoded));
    const auto &decoded_vector = std::get<vector_type>(decoded);
    REQUIRE_EQ(decoded_vector.size(), 1U);
    CHECK_FALSE(static_cast<bool>(decoded_vector[0]));
}

TEST_CASE("shared graph variants preserve malformed pointer errors") {
    using value_type = std::variant<std::shared_ptr<std::uint64_t>, std::string>;

    {
        const auto                  bytes = to_bytes("d81d00");
        auto                        dec   = make_decoder<shared_graph_codec>(bytes);
        shared_graph_decode_session decode_graph;
        value_type                  value{std::string{"before"}};
        const auto                  result = dec(as_shared_graph(decode_graph, value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        REQUIRE(std::holds_alternative<std::string>(value));
        CHECK_EQ(std::get<std::string>(value), "before");
    }

    {
        const auto                  bytes = to_bytes("d81c6161");
        auto                        dec   = make_decoder<shared_graph_codec>(bytes);
        shared_graph_decode_session decode_graph;
        value_type                  value{std::string{"before"}};
        const auto                  result = dec(as_shared_graph(decode_graph, value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_uint_on_buffer);
        REQUIRE(std::holds_alternative<std::string>(value));
        CHECK_EQ(std::get<std::string>(value), "before");
    }
}

TEST_CASE("nullable pointer codec rejects malformed wrappers") {
    {
        const auto                     bytes   = to_bytes("f6");
        std::shared_ptr<std::uint64_t> decoded = std::make_shared<std::uint64_t>(7U);
        auto                           dec     = make_decoder<nullable_ptr_codec>(bytes);
        const auto                     result  = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_array_on_buffer);
        REQUIRE(static_cast<bool>(decoded));
        CHECK_EQ(*decoded, 7U);
    }

    {
        const auto                     bytes = to_bytes("98");
        std::shared_ptr<std::uint64_t> decoded;
        auto                           dec    = make_decoder<nullable_ptr_codec>(bytes);
        const auto                     result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
    }

    {
        const auto                     bytes = to_bytes("80");
        std::shared_ptr<std::uint64_t> decoded;
        auto                           dec    = make_decoder<nullable_ptr_codec>(bytes);
        const auto                     result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::unexpected_group_size);
    }

    {
        const auto                     bytes = to_bytes("80182a");
        std::shared_ptr<std::uint64_t> decoded;
        auto                           dec    = make_decoder<nullable_ptr_codec>(bytes);
        const auto                     result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::unexpected_group_size);

        std::uint64_t following{};
        REQUIRE(dec(following));
        CHECK_EQ(following, 42U);
    }

    {
        const auto                     bytes = to_bytes("80182a");
        std::unique_ptr<std::uint64_t> decoded;
        auto                           dec    = make_decoder<nullable_ptr_codec>(bytes);
        const auto                     result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::unexpected_group_size);

        std::uint64_t following{};
        REQUIRE(dec(following));
        CHECK_EQ(following, 42U);
    }

    {
        const auto                     bytes = to_bytes("81");
        std::shared_ptr<std::uint64_t> decoded;
        auto                           dec    = make_decoder<nullable_ptr_codec>(bytes);
        const auto                     result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
    }

    {
        const auto                     bytes = to_bytes("9fff");
        std::shared_ptr<std::uint64_t> decoded;
        auto                           dec    = make_decoder<nullable_ptr_codec>(bytes);
        const auto                     result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::unexpected_group_size);
    }

    {
        const auto                     bytes = to_bytes("8200182a");
        std::shared_ptr<std::uint64_t> decoded;
        auto                           dec    = make_decoder<nullable_ptr_codec>(bytes);
        const auto                     result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::unexpected_group_size);
    }

    {
        const auto                     bytes = to_bytes("8101");
        std::shared_ptr<std::uint64_t> decoded;
        auto                           dec    = make_decoder<nullable_ptr_codec>(bytes);
        const auto                     result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::unexpected_group_size);
    }

    {
        const auto                     bytes = to_bytes("8102");
        std::shared_ptr<std::uint64_t> decoded;
        auto                           dec    = make_decoder<nullable_ptr_codec>(bytes);
        const auto                     result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
    }
}

TEST_CASE("nullable and shared graph codecs compose") {
    using combined_encoder = decltype(make_encoder<nullable_ptr_codec, shared_graph_codec>(std::declval<std::vector<std::byte> &>()));
    using combined_decoder = decltype(make_decoder<nullable_ptr_codec, shared_graph_codec>(std::declval<std::vector<std::byte> &>()));
    using reversed_encoder = decltype(make_encoder<shared_graph_codec, nullable_ptr_codec>(std::declval<std::vector<std::byte> &>()));
    using reversed_decoder = decltype(make_decoder<shared_graph_codec, nullable_ptr_codec>(std::declval<std::vector<std::byte> &>()));

    static_assert(smart_ptr_test::CanEncode<combined_encoder, std::unique_ptr<std::uint64_t>>);
    static_assert(smart_ptr_test::CanEncode<combined_encoder, std::shared_ptr<std::uint64_t>>);
    static_assert(smart_ptr_test::CanDecode<combined_decoder, std::unique_ptr<std::uint64_t>>);
    static_assert(smart_ptr_test::CanDecode<combined_decoder, std::shared_ptr<std::uint64_t>>);
    static_assert(smart_ptr_test::CanEncode<reversed_encoder, std::unique_ptr<std::uint64_t>>);
    static_assert(smart_ptr_test::CanEncode<reversed_encoder, std::shared_ptr<std::uint64_t>>);
    static_assert(smart_ptr_test::CanDecode<reversed_decoder, std::unique_ptr<std::uint64_t>>);
    static_assert(smart_ptr_test::CanDecode<reversed_decoder, std::shared_ptr<std::uint64_t>>);

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder<nullable_ptr_codec, shared_graph_codec>(buffer);
    REQUIRE(enc(std::shared_ptr<std::uint64_t>{}));
    REQUIRE(enc(std::make_shared<std::uint64_t>(42U)));
    CHECK_EQ(to_hex(buffer), "81008201182a");

    std::shared_ptr<std::uint64_t> decoded_null = std::make_shared<std::uint64_t>(7U);
    std::shared_ptr<std::uint64_t> decoded_value;
    auto                           dec = make_decoder<nullable_ptr_codec, shared_graph_codec>(buffer);
    REQUIRE(dec(decoded_null));
    REQUIRE(dec(decoded_value));
    CHECK_FALSE(static_cast<bool>(decoded_null));
    REQUIRE(static_cast<bool>(decoded_value));
    CHECK_EQ(*decoded_value, 42U);

    std::vector<std::byte> reversed_buffer;
    auto                   reversed_enc = make_encoder<shared_graph_codec, nullable_ptr_codec>(reversed_buffer);
    REQUIRE(reversed_enc(std::make_shared<std::uint64_t>(13U)));
    CHECK_EQ(to_hex(reversed_buffer), "82010d");
}

TEST_CASE("combined codecs use nullable variant dispatch outside graph wrappers") {
    using value_type = std::variant<std::shared_ptr<std::uint64_t>, static_tag<42>>;

    static_assert(!std::is_aggregate_v<shared_graph_encode_root<std::uint64_t>>);
    static_assert(!std::is_aggregate_v<shared_graph_decode_root<std::uint64_t>>);
    static_assert(!smart_ptr_detail::variant_has_shared_graph_tag_collision_v<std::shared_ptr<std::uint64_t>, static_tag<42>>);
    static_assert(smart_ptr_detail::variant_has_shared_graph_tag_collision_v<std::shared_ptr<std::uint64_t>, static_tag<28>>);
    static_assert(smart_ptr_detail::variant_has_shared_graph_tag_collision_v<std::shared_ptr<std::uint64_t>, static_tag<29>>);
    static_assert(smart_ptr_detail::variant_has_shared_graph_tag_collision_v<std::shared_ptr<std::uint64_t>, as_tag_any>);

    {
        const auto bytes = to_bytes("d82a");
        auto       dec   = make_decoder<nullable_ptr_codec, shared_graph_codec>(bytes);
        value_type value{std::shared_ptr<std::uint64_t>{}};

        REQUIRE(dec(value));
        CHECK(std::holds_alternative<static_tag<42>>(value));
    }

    {
        const auto bytes = to_bytes("8100");
        auto       dec   = make_decoder<nullable_ptr_codec, shared_graph_codec>(bytes);
        value_type value{static_tag<42>{}};

        REQUIRE(dec(value));
        REQUIRE(std::holds_alternative<std::shared_ptr<std::uint64_t>>(value));
        CHECK_FALSE(static_cast<bool>(std::get<std::shared_ptr<std::uint64_t>>(value)));
    }

    {
        using graph_tag_value_type = std::variant<std::shared_ptr<std::uint64_t>, static_tag<28>>;

        const auto           bytes = to_bytes("d81c");
        auto                 dec   = make_decoder<nullable_ptr_codec, shared_graph_codec>(bytes);
        graph_tag_value_type value{std::shared_ptr<std::uint64_t>{}};

        REQUIRE(dec(value));
        CHECK(std::holds_alternative<static_tag<28>>(value));
    }

    {
        using graph_ref_tag_value_type = std::variant<std::shared_ptr<std::uint64_t>, static_tag<29>>;

        const auto               bytes = to_bytes("d81d");
        auto                     dec   = make_decoder<nullable_ptr_codec, shared_graph_codec>(bytes);
        graph_ref_tag_value_type value{std::shared_ptr<std::uint64_t>{}};

        REQUIRE(dec(value));
        CHECK(std::holds_alternative<static_tag<29>>(value));
    }

    {
        using any_tag_value_type = std::variant<std::shared_ptr<std::uint64_t>, as_tag_any>;

        const auto         bytes = to_bytes("d82a");
        auto               dec   = make_decoder<nullable_ptr_codec, shared_graph_codec>(bytes);
        any_tag_value_type value{std::shared_ptr<std::uint64_t>{}};

        REQUIRE(dec(value));
        REQUIRE(std::holds_alternative<as_tag_any>(value));
        CHECK_EQ(std::get<as_tag_any>(value).tag, 42U);
    }
}

TEST_CASE("shared graph codec rejects inactive nullable variant dispatch without nullable codec") {
    {
        using value_type = std::variant<std::shared_ptr<std::uint64_t>, std::string>;

        const auto bytes = to_bytes("8100");
        auto       dec   = make_decoder<shared_graph_codec>(bytes);
        value_type value{std::string{"before"}};
        const auto result = dec(value);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        REQUIRE(std::holds_alternative<std::string>(value));
        CHECK_EQ(std::get<std::string>(value), "before");
    }

    {
        using value_type = std::variant<std::shared_ptr<std::uint64_t>, static_tag<42>>;

        const auto bytes = to_bytes("d82a");
        auto       dec   = make_decoder<shared_graph_codec>(bytes);
        value_type value{std::shared_ptr<std::uint64_t>{}};
        const auto result = dec(value);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK(std::holds_alternative<std::shared_ptr<std::uint64_t>>(value));
    }

    {
        using value_type = std::variant<std::vector<std::shared_ptr<std::uint64_t>>, std::string>;

        const auto bytes = to_bytes("80");
        auto       dec   = make_decoder<shared_graph_codec>(bytes);
        value_type value{std::string{"before"}};
        const auto result = dec(value);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        REQUIRE(std::holds_alternative<std::string>(value));
        CHECK_EQ(std::get<std::string>(value), "before");
    }
}

TEST_CASE("shared graph variants allow non-colliding tag alternatives") {
    using value_type = std::variant<std::shared_ptr<std::uint64_t>, static_tag<42>, std::string>;

    {
        const auto                  bytes = to_bytes("626f6b");
        auto                        dec   = make_decoder<nullable_ptr_codec, shared_graph_codec>(bytes);
        shared_graph_decode_session decode_graph;
        value_type                  value{std::shared_ptr<std::uint64_t>{}};

        REQUIRE(dec(as_shared_graph(decode_graph, value)));
        REQUIRE(std::holds_alternative<std::string>(value));
        CHECK_EQ(std::get<std::string>(value), "ok");
    }

    {
        const auto                  bytes = to_bytes("d82a");
        auto                        dec   = make_decoder<nullable_ptr_codec, shared_graph_codec>(bytes);
        shared_graph_decode_session decode_graph;
        value_type                  value{std::shared_ptr<std::uint64_t>{}};

        REQUIRE(dec(as_shared_graph(decode_graph, value)));
        CHECK(std::holds_alternative<static_tag<42>>(value));
    }

    {
        const auto                  bytes = to_bytes("8100");
        auto                        dec   = make_decoder<nullable_ptr_codec, shared_graph_codec>(bytes);
        shared_graph_decode_session decode_graph;
        value_type                  value{std::string{"before"}};

        REQUIRE(dec(as_shared_graph(decode_graph, value)));
        REQUIRE(std::holds_alternative<std::shared_ptr<std::uint64_t>>(value));
        CHECK_FALSE(static_cast<bool>(std::get<std::shared_ptr<std::uint64_t>>(value)));
    }

    {
        const auto                  bytes = to_bytes("d81c182ad81d00");
        auto                        dec   = make_decoder<nullable_ptr_codec, shared_graph_codec>(bytes);
        shared_graph_decode_session decode_graph;
        value_type                  first{std::string{"first"}};
        value_type                  second{std::string{"second"}};

        REQUIRE(dec(as_shared_graph(decode_graph, first)));
        REQUIRE(dec(as_shared_graph(decode_graph, second)));
        REQUIRE(std::holds_alternative<std::shared_ptr<std::uint64_t>>(first));
        REQUIRE(std::holds_alternative<std::shared_ptr<std::uint64_t>>(second));
        const auto &first_ptr  = std::get<std::shared_ptr<std::uint64_t>>(first);
        const auto &second_ptr = std::get<std::shared_ptr<std::uint64_t>>(second);
        REQUIRE(static_cast<bool>(first_ptr));
        REQUIRE(static_cast<bool>(second_ptr));
        CHECK_EQ(*first_ptr, 42U);
        CHECK(first_ptr.get() == second_ptr.get());
    }
}

TEST_CASE("shared graph variants reject malformed nullable arrays") {
    using value_type = std::variant<std::shared_ptr<std::uint64_t>, static_tag<42>, std::string>;

    {
        const auto                  bytes = to_bytes("8201182a");
        auto                        dec   = make_decoder<nullable_ptr_codec, shared_graph_codec>(bytes);
        shared_graph_decode_session decode_graph;
        value_type                  value{std::string{"before"}};
        const auto                  result = dec(as_shared_graph(decode_graph, value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::unexpected_group_size);
        REQUIRE(std::holds_alternative<std::string>(value));
        CHECK_EQ(std::get<std::string>(value), "before");
    }

    {
        const auto                  bytes = to_bytes("8101");
        auto                        dec   = make_decoder<nullable_ptr_codec, shared_graph_codec>(bytes);
        shared_graph_decode_session decode_graph;
        value_type                  value{std::string{"before"}};
        const auto                  result = dec(as_shared_graph(decode_graph, value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        REQUIRE(std::holds_alternative<std::string>(value));
        CHECK_EQ(std::get<std::string>(value), "before");
    }
}

TEST_CASE("shared graph variants dispatch nested non-colliding tag alternatives") {
    using nested_type = std::variant<std::string, static_tag<42>>;
    using value_type  = std::variant<std::shared_ptr<std::uint64_t>, nested_type>;

    static_assert(!smart_ptr_detail::variant_has_shared_graph_tag_collision_v<std::shared_ptr<std::uint64_t>, nested_type>);
    static_assert(smart_ptr_detail::variant_has_shared_graph_tag_collision_v<std::shared_ptr<std::uint64_t>,
                                                                             std::variant<std::string, static_tag<28>>>);
    static_assert(smart_ptr_detail::variant_has_shared_graph_tag_collision_v<std::shared_ptr<std::uint64_t>,
                                                                             std::variant<std::string, static_tag<29>>>);
    static_assert(
        smart_ptr_detail::variant_has_shared_graph_tag_collision_v<std::shared_ptr<std::uint64_t>, std::variant<std::string, as_tag_any>>);

    const auto                  bytes = to_bytes("d82a");
    auto                        dec   = make_decoder<nullable_ptr_codec, shared_graph_codec>(bytes);
    shared_graph_decode_session decode_graph;
    value_type                  value{std::shared_ptr<std::uint64_t>{}};

    REQUIRE(dec(as_shared_graph(decode_graph, value)));
    REQUIRE(std::holds_alternative<nested_type>(value));
    const auto &nested = std::get<nested_type>(value);
    CHECK(std::holds_alternative<static_tag<42>>(nested));
}

TEST_CASE("shared graph rejects variants with nested graph tag collisions") {
    {
        using nested_type = std::variant<std::string, static_tag<28>>;
        using value_type  = std::variant<std::shared_ptr<std::uint64_t>, nested_type>;

        const auto                  bytes = to_bytes("d81c182a");
        auto                        dec   = make_decoder<nullable_ptr_codec, shared_graph_codec>(bytes);
        shared_graph_decode_session decode_graph;
        value_type                  value{std::shared_ptr<std::uint64_t>{}};
        const auto                  result = dec(as_shared_graph(decode_graph, value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK(std::holds_alternative<std::shared_ptr<std::uint64_t>>(value));
    }

    {
        using nested_type = std::variant<std::string, static_tag<29>>;
        using value_type  = std::variant<std::shared_ptr<std::uint64_t>, nested_type>;

        const auto                     bytes = to_bytes("d81c182ad81d00");
        auto                           dec   = make_decoder<nullable_ptr_codec, shared_graph_codec>(bytes);
        shared_graph_decode_session    decode_graph;
        std::shared_ptr<std::uint64_t> seeded;

        REQUIRE(dec(as_shared_graph(decode_graph, seeded)));
        REQUIRE(static_cast<bool>(seeded));
        CHECK_EQ(*seeded, 42U);

        value_type value{std::shared_ptr<std::uint64_t>{}};
        const auto result = dec(as_shared_graph(decode_graph, value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK(std::holds_alternative<std::shared_ptr<std::uint64_t>>(value));
    }

    {
        using nested_type = std::variant<std::string, as_tag_any>;
        using value_type  = std::variant<std::shared_ptr<std::uint64_t>, nested_type>;

        const auto                  bytes = to_bytes("d82a");
        auto                        dec   = make_decoder<nullable_ptr_codec, shared_graph_codec>(bytes);
        shared_graph_decode_session decode_graph;
        value_type                  value{std::shared_ptr<std::uint64_t>{}};
        const auto                  result = dec(as_shared_graph(decode_graph, value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK(std::holds_alternative<std::shared_ptr<std::uint64_t>>(value));
    }
}

TEST_CASE("shared graph rejects variants with graph tag collisions") {
    {
        using value_type = std::variant<std::shared_ptr<std::uint64_t>, static_tag<28>>;

        const auto                  bytes = to_bytes("d81c182a");
        auto                        dec   = make_decoder<nullable_ptr_codec, shared_graph_codec>(bytes);
        shared_graph_decode_session decode_graph;
        value_type                  value{std::shared_ptr<std::uint64_t>{}};
        const auto                  result = dec(as_shared_graph(decode_graph, value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK(std::holds_alternative<std::shared_ptr<std::uint64_t>>(value));
    }

    {
        using value_type = std::variant<std::shared_ptr<std::uint64_t>, static_tag<29>>;

        const auto                     bytes = to_bytes("d81c182ad81d00");
        auto                           dec   = make_decoder<nullable_ptr_codec, shared_graph_codec>(bytes);
        shared_graph_decode_session    decode_graph;
        std::shared_ptr<std::uint64_t> seeded;

        REQUIRE(dec(as_shared_graph(decode_graph, seeded)));
        REQUIRE(static_cast<bool>(seeded));
        CHECK_EQ(*seeded, 42U);

        value_type value{static_tag<29>{}};
        const auto result = dec(as_shared_graph(decode_graph, value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK(std::holds_alternative<static_tag<29>>(value));
    }

    {
        using value_type = std::variant<std::shared_ptr<std::uint64_t>, as_tag_any>;

        const auto                  bytes = to_bytes("d82a");
        auto                        dec   = make_decoder<nullable_ptr_codec, shared_graph_codec>(bytes);
        shared_graph_decode_session decode_graph;
        value_type                  value{std::shared_ptr<std::uint64_t>{}};
        const auto                  result = dec(as_shared_graph(decode_graph, value));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK(std::holds_alternative<std::shared_ptr<std::uint64_t>>(value));
    }
}

TEST_CASE("nullable and shared graph codecs use graph identity inside graph wrappers") {
    auto shared = std::make_shared<std::uint64_t>(42U);

    std::vector<std::byte>      buffer;
    auto                        enc = make_encoder<nullable_ptr_codec, shared_graph_codec>(buffer);
    shared_graph_encode_session encode_graph;

    REQUIRE(enc(as_shared_graph(encode_graph, shared)));
    REQUIRE(enc(as_shared_graph(encode_graph, shared)));
    CHECK_EQ(to_hex(buffer), "d81c182ad81d00");

    std::shared_ptr<std::uint64_t> decoded_first;
    std::shared_ptr<std::uint64_t> decoded_second;
    auto                           dec = make_decoder<nullable_ptr_codec, shared_graph_codec>(buffer);
    shared_graph_decode_session    decode_graph;

    REQUIRE(dec(as_shared_graph(decode_graph, decoded_first)));
    REQUIRE(dec(as_shared_graph(decode_graph, decoded_second)));
    REQUIRE(static_cast<bool>(decoded_first));
    REQUIRE(static_cast<bool>(decoded_second));
    CHECK(decoded_first.get() == decoded_second.get());
}
