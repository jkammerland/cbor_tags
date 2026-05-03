
#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_encoder.h"
#include "test_util.h"

#include <array>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <doctest/parts/doctest_fwd.h>
#include <fmt/base.h>
#include <fmt/core.h>
#include <list>
#include <map>
#include <memory_resource>
#include <nameof.hpp>
#include <numeric>
#include <optional>
#include <vector>

using namespace cbor::tags;

namespace {
consteval bool negative_wrapper_value_is_representable(std::uint64_t value) { return value != 0; }

struct MemberEncodeOnly {
    int value{7};

    template <typename Encoder> constexpr auto encode(Encoder &enc) const { return enc(value); }
};

struct FreeEncodeOnly {
    int value{8};
};

template <typename Encoder> constexpr auto encode(Encoder &enc, const FreeEncodeOnly &value) { return enc(value.value); }

struct FreeTranscodeOnly {
    int value{9};
};

template <typename Transcoder> constexpr auto transcode(Transcoder &transcoder, const FreeTranscodeOnly &value) {
    return transcoder(value.value);
}

struct TaggedMemberEncode {
    static_tag<99> cbor_tag;
    int            value{1};

    template <typename Encoder> constexpr auto encode(Encoder &enc) const { return enc(value); }
};
} // namespace

static_assert(negative_wrapper_value_is_representable(1));
static_assert(!negative_wrapper_value_is_representable(0));

TEST_CASE_TEMPLATE("CBOR Encoder", T, std::vector<std::byte>, std::deque<std::byte>, std::array<std::byte, 1024>) {
    auto data = T{};
    auto enc  = make_encoder(data);

    enc.encode(static_cast<std::uint64_t>(1));
    enc.encode(static_cast<std::uint64_t>(2));
    enc.encode(static_cast<std::uint64_t>(3));

    if constexpr (!IsFixedArray<T>) {
        CHECK_EQ(to_hex(data), "010203");
    } else {
        CHECK_EQ(to_hex(std::span(data.data(), 3)), "010203");
    }
}

TEST_CASE_TEMPLATE("CBOR Encoder array/vector buffer", T, std::vector<std::byte>, std::array<std::byte, 1024>) {
    auto data = T{};
    auto enc  = make_encoder(data);

    using namespace std::string_view_literals;
    auto sv = "Hello world!"sv;
    enc.encode(sv);

    if constexpr (!IsFixedArray<T>) {
        CHECK_EQ(to_hex(data), "6c48656c6c6f20776f726c6421");
        // +1 for the tag
        CHECK_EQ(std::string_view(reinterpret_cast<const char *>(data.data() + 1), 12), sv);
    } else {
        // +1 for the tag
        CHECK_EQ(to_hex(std::span(data.data(), sv.size() + 1)), "6c48656c6c6f20776f726c6421");
        CHECK_EQ(std::string_view(reinterpret_cast<const char *>(data.data() + 1), 12), sv);
    }
}

TEST_CASE("CBOR Encoder documents zero negative wrapper edge behavior") {
    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);

    auto result = enc(negative{0});

    REQUIRE(result);
    CHECK_EQ(to_hex(data), "3bffffffffffffffff");
}

TEST_CASE("CBOR Encoder writes exact-size fixed buffers") {
    std::array<std::byte, 2> buffer{};
    auto                     enc = make_encoder(buffer);

    auto result = enc(std::uint8_t{24});

    REQUIRE(result);
    CHECK_EQ(to_hex(buffer), "1818");
}

TEST_CASE("CBOR Encoder writes floating-point major type payloads") {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);

    REQUIRE(enc(float16_t{static_cast<std::uint16_t>(0x3C00)}, float{1.0F}, double{1.0}));
    CHECK_EQ(to_hex(buffer), "f93c00fa3f800000fb3ff0000000000000");
}

TEST_CASE("CBOR Encoder writes indefinite wrappers") {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    std::vector<int>       values{1, 2};
    std::map<int, int>     mapping{{1, 2}};
    std::string            text{"hi"};
    std::vector<std::byte> bytes{std::byte{0xAB}, std::byte{0xCD}};

    REQUIRE(enc(as_indefinite{values}, as_indefinite{mapping}, as_indefinite{text}, as_indefinite{bytes}));
    CHECK_EQ(to_hex(buffer), "9f0102ffbf0102ff7f626869ff5f42abcdff");
}

TEST_CASE("CBOR Encoder dispatches class encoding overload forms") {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);

    REQUIRE(enc(MemberEncodeOnly{}, FreeEncodeOnly{}, FreeTranscodeOnly{}, TaggedMemberEncode{}));
    CHECK_EQ(to_hex(buffer), "070809d86301");
}

TEST_CASE("CBOR Encoder rejects invalid indefinite wrapper type") {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    int                    value{1};

    auto result = enc(as_indefinite{value});

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::error);
}

TEST_CASE("CBOR Encoder on deque") {
    std::deque<std::byte> buffer;
    auto                  enc = make_encoder(buffer);

    using namespace std::string_view_literals;
    auto sv = "Hello world!"sv;
    enc.encode(static_cast<std::uint64_t>(1));
    enc.encode(sv);

    CHECK_EQ(to_hex(buffer), "016c48656c6c6f20776f726c6421");

    { // Big string
        auto        size1 = buffer.size();
        std::string big_string(10, 'a');
        enc.encode(big_string);
        auto size2 = buffer.size();

        CHECK_EQ(size2 - size1, 1 + big_string.size());
        // CBOR_TAGS_TEST_LOG("big_string: {}\n", to_hex(buffer));
    }
}

TEST_CASE_TEMPLATE("CBOR with std::pmr", T, std::pmr::vector<std::byte>, std::pmr::deque<char>, std::pmr::deque<uint8_t>,
                   std::pmr::list<char>, std::pmr::list<uint8_t>) {
    CBOR_TAGS_TEST_LOG("Testing with T: {}\n", nameof::nameof_type<T>());

    std::array<std::byte, 1024>         buffer;
    std::pmr::monotonic_buffer_resource resource(buffer.data(), buffer.size());
    T                                   resource_vector(&resource);

    auto enc = make_encoder<T>(resource_vector);
    enc.encode(static_cast<std::uint64_t>(1));
    enc.encode(static_cast<std::uint64_t>(2));
    enc.encode(static_cast<std::uint64_t>(3));

    CHECK_EQ(to_hex(resource_vector), "010203");
}

TEST_CASE_TEMPLATE("CBOR test mix types with containers", T, std::vector<std::byte>, std::deque<std::byte>) {
    std::array<uint64_t, 100> buffer_array;
    std::list<uint64_t>       buffer_list(100);

    {
        T    data;
        auto enc = make_encoder(data);

        std::iota(buffer_array.begin(), buffer_array.end(), 0);
        auto span = std::span(buffer_array);

        enc.encode(span);
        // CBOR_TAGS_TEST_LOG("span: {}\n", to_hex(data));
        CHECK_EQ(
            to_hex(data),
            "9864000102030405060708090a0b0c0d0e0f101112131415161718181819181a181b181c181d181e181f18201821182218231824182518261827182818291"
            "82a182b182c182d182e182f1830183118321833183418351836183718381839183a183b183c183d183e183f18401841184218431844184518461847184818"
            "49184a184b184c184d184e184f1850185118521853185418551856185718581859185a185b185c185d185e185f1860186118621863");
    }
    {
        T    data;
        auto enc = make_encoder(data);

        std::iota(buffer_list.begin(), buffer_list.end(), 0);

        enc.encode(buffer_list);
        // CBOR_TAGS_TEST_LOG("list: {}\n", to_hex(data));
        CHECK_EQ(to_hex(data),
                 "9864000102030405060708090a0b0c0d0e0f101112131415161718181819181a181b181c181d181e181f1820182118221823182418251826182718281"
                 "829182a182b182c182d182e182f1830183118321833183418351836183718381839183a183b183c183d183e183f184018411842184318441845184618"
                 "4718481849184a184b184c184d184e184f1850185118521853185418551856185718581859185a185b185c185d185e185f1860186118621863");
    }

    CHECK_EQ(std::equal(buffer_array.begin(), buffer_array.end(), buffer_list.begin()), true);
}

TEST_CASE_TEMPLATE("Test variant<...>", T, std::array<uint8_t, 1024>, std::vector<uint8_t>) {
    using namespace std::string_view_literals;
    using variant_t = std::variant<int, double, std::string, std::optional<uint8_t>>;

    {
        T    buffer{};
        auto enc = make_encoder(buffer);

        variant_t var = 42;
        enc.encode(var);

        auto expected_sv = "182a"sv;
        CHECK_EQ(to_hex(buffer).substr(0, expected_sv.size()), expected_sv);
    }
    {
        T    buffer{};
        auto enc = make_encoder(buffer);

        variant_t var = 3.14;
        enc.encode(var);

        auto expected_sv = "fb40091eb851eb851f"sv;
        CHECK_EQ(to_hex(buffer).substr(0, expected_sv.size()), expected_sv);
    }
    {
        T    buffer{};
        auto enc = make_encoder(buffer);

        variant_t var = "Hello world!";
        enc.encode(var);

        auto expected = "6c48656c6c6f20776f726c6421"sv;
        CHECK_EQ(to_hex(buffer).substr(0, expected.size()), expected);
    }

    {
        T    buffer{};
        auto enc = make_encoder(buffer);

        variant_t var = std::optional<uint8_t>(42);
        enc.encode(var);

        auto expected_sv = "182a"sv;
        CHECK_EQ(to_hex(buffer).substr(0, expected_sv.size()), expected_sv);
    }

    {
        T    buffer{};
        auto enc = make_encoder(buffer);

        variant_t var = std::nullopt;
        enc.encode(var);

        auto expected_sv = "f6"sv;
        CHECK_EQ(to_hex(buffer).substr(0, expected_sv.size()), expected_sv);
    }
}

TEST_SUITE("Encoding Classes") {
    struct Class123 {
        int    a{4};
        double b{5.0};

      private:
        std::string c{"hello"};

        friend cbor::tags::Access;
        template <typename T> constexpr auto transcode(T &transcoder) const { return transcoder(wrap_as_array{a, b, c}); }
    };

    TEST_CASE("Class123") {
        auto buffer = std::vector<uint8_t>{};
        auto enc    = make_encoder(buffer);

        REQUIRE(enc(Class123{}));

        REQUIRE_GE(buffer.size(), 0);
        CHECK_EQ("8304fb40140000000000006568656c6c6f", to_hex(buffer));
    }
}
