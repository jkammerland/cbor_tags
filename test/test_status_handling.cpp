#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_integer.h"
#include "magic_enum/magic_enum.hpp"
#include "test_util.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <fmt/base.h>
#include <list>
#include <map>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

using namespace cbor::tags;
using namespace cbor::tags::literals;
using namespace std::string_view_literals;

namespace {

std::vector<std::byte> uint64_max_array_header() {
    return {std::byte{0x9B}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
            std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
}

std::vector<std::byte> uint64_max_map_header_with_one_pair() {
    auto buffer = std::vector<std::byte>{std::byte{0xBB}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
                                         std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
    buffer.push_back(std::byte{0x01});
    buffer.push_back(std::byte{0x02});
    return buffer;
}

template <typename T, std::size_t StorageSize = 16> void check_decode_out_of_memory(const std::vector<std::byte> &data) {
    std::array<std::byte, StorageSize>  storage{};
    std::pmr::monotonic_buffer_resource resource(storage.data(), storage.size(), std::pmr::null_memory_resource());
    T                                   decoded(&resource);

    auto dec    = make_decoder(data);
    auto result = dec(decoded);

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::out_of_memory);
}

struct throwing_memory_resource : std::pmr::memory_resource {
  private:
    void *do_allocate(std::size_t, std::size_t) override { throw std::bad_alloc{}; }
    void  do_deallocate(void *, std::size_t, std::size_t) override {}
    bool  do_is_equal(const std::pmr::memory_resource &other) const noexcept override { return this == &other; }
};

struct fail_after_construction_memory_resource : std::pmr::memory_resource {
    std::pmr::memory_resource *upstream;
    bool                       fail_allocations{};

    explicit fail_after_construction_memory_resource(std::pmr::memory_resource *resource = std::pmr::new_delete_resource())
        : upstream(resource) {}

  private:
    void *do_allocate(std::size_t bytes, std::size_t alignment) override {
        if (fail_allocations) {
            throw std::bad_alloc{};
        }
        return upstream->allocate(bytes, alignment);
    }

    void do_deallocate(void *ptr, std::size_t bytes, std::size_t alignment) override { upstream->deallocate(ptr, bytes, alignment); }

    bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override { return this == &other; }
};

template <typename T> void check_decode_out_of_memory_after_construction(const std::vector<std::byte> &data) {
    fail_after_construction_memory_resource resource;
    T                                       decoded(&resource);
    resource.fail_allocations = true;

    auto dec    = make_decoder(data);
    auto result = dec(decoded);

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::out_of_memory);
}

struct default_memory_resource_guard {
    std::pmr::memory_resource *previous;

    explicit default_memory_resource_guard(std::pmr::memory_resource *resource) : previous(std::pmr::set_default_resource(resource)) {}
    ~default_memory_resource_guard() { std::pmr::set_default_resource(previous); }
};

} // namespace

TEST_CASE("status messages cover every declared status code") {
    constexpr status_code statuses[] = {
        status_code::success,
        status_code::incomplete,
        status_code::unexpected_group_size,
        status_code::out_of_memory,
        status_code::error,
        status_code::contiguous_view_on_non_contiguous_data,
        status_code::invalid_utf8_sequence,
        status_code::begin_no_match_decoding,
        status_code::no_match_for_tag,
        status_code::no_match_for_tag_simple_on_buffer,
        status_code::no_match_for_uint_on_buffer,
        status_code::no_match_for_nint_on_buffer,
        status_code::no_match_for_int_on_buffer,
        status_code::no_match_for_enum_on_buffer,
        status_code::no_match_for_bstr_on_buffer,
        status_code::no_match_for_tstr_on_buffer,
        status_code::no_match_for_array_on_buffer,
        status_code::no_match_for_map_on_buffer,
        status_code::no_match_for_tag_on_buffer,
        status_code::no_match_for_simple_on_buffer,
        status_code::no_match_for_optional_on_buffer,
        status_code::no_match_in_variant_on_buffer,
        status_code::end_no_match_decoding,
        status_code::size_limit_exceeded,
    };

    for (auto status : statuses) {
        CHECK_NE(status_message(status), "Unknown CBOR status code"sv);
    }
    CHECK_EQ(status_message(static_cast<status_code>(255)), "Unknown CBOR status code"sv);
}

TEST_CASE("variant mismatch classification uses the no-match status range") {
    CHECK_FALSE(detail::is_retriable_variant_mismatch(status_code::begin_no_match_decoding));
    CHECK(detail::is_retriable_variant_mismatch(status_code::no_match_for_tag));
    CHECK(detail::is_retriable_variant_mismatch(status_code::no_match_in_variant_on_buffer));
    CHECK_FALSE(detail::is_retriable_variant_mismatch(status_code::end_no_match_decoding));
    CHECK_FALSE(detail::is_retriable_variant_mismatch(status_code::unexpected_group_size));
}

TEST_CASE("nested variants dispatch through core tag alternatives") {
    using nested_type = std::variant<static_tag<42>, std::string>;
    using value_type  = std::variant<std::uint64_t, nested_type>;

    const auto bytes = to_bytes("d82a");
    value_type value{std::uint64_t{}};
    auto       dec = make_decoder(bytes);

    REQUIRE(dec(value));
    REQUIRE(std::holds_alternative<nested_type>(value));
    const auto &nested = std::get<nested_type>(value);
    CHECK(std::holds_alternative<static_tag<42>>(nested));
}

TEST_SUITE("Decoding the wrong thing") {
    TEST_CASE("Decode wrong tag") {
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        REQUIRE(enc(140_tag, "Hello world!"sv));

        auto        dec = make_decoder(data);
        std::string result;
        auto        result2 = dec(141_tag, result);
        CHECK(!result2);

        { /* Sanity check recovery */
            auto result3 = dec(result);
            CHECK(result3);
            CHECK_EQ(result, "Hello world!");
        }
    }

    TEST_CASE_TEMPLATE("Decode wrong major types, from int", T, negative, std::string, std::vector<std::byte>, std::map<int, int>,
                       static_tag<140>, float) {
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        REQUIRE(enc(int{140}));

        auto dec    = make_decoder(data);
        auto result = dec(T{});
        REQUIRE(!result);
        if constexpr (IsNegative<T>) {
            CHECK_EQ(result.error(), status_code::no_match_for_nint_on_buffer);
        } else if constexpr (IsTextString<T>) {
            CHECK_EQ(result.error(), status_code::no_match_for_tstr_on_buffer);
        } else if constexpr (IsBinaryString<T>) {
            CHECK_EQ(result.error(), status_code::no_match_for_bstr_on_buffer);
        } else if constexpr (IsMap<T>) {
            CHECK_EQ(result.error(), status_code::no_match_for_map_on_buffer);
        } else if constexpr (IsTag<T>) {
            CHECK_EQ(result.error(), status_code::no_match_for_tag_on_buffer);
        } else if constexpr (IsSimple<T>) {
            CHECK_EQ(result.error(), status_code::no_match_for_simple_on_buffer);
        }
    }

    TEST_CASE_TEMPLATE("Decode wrong major types, from tag", T, positive, negative, std::string_view, std::vector<std::byte>,
                       std::map<int, int>, bool, std::nullptr_t, double) {
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        REQUIRE(enc(make_tag_pair(140_tag, 42)));

        auto dec    = make_decoder(data);
        auto result = dec(T{});
        REQUIRE(!result);
        if constexpr (IsUnsigned<T>) {
            CHECK_EQ(result.error(), status_code::no_match_for_uint_on_buffer);
        } else if constexpr (IsNegative<T>) {
            CHECK_EQ(result.error(), status_code::no_match_for_nint_on_buffer);
        } else if constexpr (IsTextString<T>) {
            CHECK_EQ(result.error(), status_code::no_match_for_tstr_on_buffer);
        } else if constexpr (IsBinaryString<T>) {
            CHECK_EQ(result.error(), status_code::no_match_for_bstr_on_buffer);
        } else if constexpr (IsMap<T>) {
            CHECK_EQ(result.error(), status_code::no_match_for_map_on_buffer);
        } else if constexpr (IsSimple<T>) {
            CHECK_EQ(result.error(), status_code::no_match_for_simple_on_buffer);
        }
    }

    TEST_CASE_TEMPLATE("Decode wrong simple tag", T, float, double, bool, std::nullptr_t) {
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        REQUIRE(enc(25_tag, float16_t{3.1f}));

        auto dec    = make_decoder(data);
        auto result = dec(25_tag, T{});
        REQUIRE(!result);
        CHECK_EQ(result.error(), status_code::no_match_for_tag_simple_on_buffer);
    }

    TEST_CASE_TEMPLATE("Decode, too little memory", T, std::pmr::vector<int>) {
        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);
        REQUIRE(enc(T{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));

        // Limit memory for decoding
        std::array<std::byte, 16>           R;
        std::pmr::monotonic_buffer_resource resource(R.data(), R.size(), std::pmr::null_memory_resource());
        T                                   our_decoded_array(&resource);

        auto dec    = make_decoder(data);
        auto result = dec(our_decoded_array);
        REQUIRE(!result);
        CHECK_EQ(result.error(), status_code::out_of_memory);
    }

    TEST_CASE("Decode bounded pmr array reserve failure returns out_of_memory") {
        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);
        REQUIRE(enc(std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8}));

        check_decode_out_of_memory<std::pmr::vector<int>>(data);
    }

    TEST_CASE("Decode bounded pmr array length_error returns out_of_memory") {
        check_decode_out_of_memory<std::pmr::vector<int>>(uint64_max_array_header());
    }

    TEST_CASE("Decode pmr array append failure returns out_of_memory") {
        const auto definite_array   = std::vector<std::byte>{std::byte{0x81}, std::byte{0x01}};
        const auto indefinite_array = std::vector<std::byte>{std::byte{0x9F}, std::byte{0x01}, std::byte{0x02}, std::byte{0xFF}};

        check_decode_out_of_memory_after_construction<std::pmr::list<int>>(definite_array);
        check_decode_out_of_memory_after_construction<std::pmr::vector<int>>(indefinite_array);
    }

    TEST_CASE("Decode pmr map insert failure returns out_of_memory") {
        const auto definite_map   = std::vector<std::byte>{std::byte{0xA1}, std::byte{0x01}, std::byte{0x02}};
        const auto indefinite_map = std::vector<std::byte>{std::byte{0xBF}, std::byte{0x01}, std::byte{0x02}, std::byte{0xFF}};

        check_decode_out_of_memory_after_construction<std::pmr::map<int, int>>(definite_map);
        check_decode_out_of_memory_after_construction<std::pmr::map<int, int>>(indefinite_map);
        check_decode_out_of_memory_after_construction<std::pmr::map<int, int>>(uint64_max_map_header_with_one_pair());
    }

    TEST_CASE("Decode bounded pmr strings return out_of_memory") {
        {
            std::vector<std::byte> data;
            auto                   enc = make_encoder(data);
            REQUIRE(enc(std::string(64, 'x')));

            check_decode_out_of_memory<std::pmr::string>(data);
        }

        {
            std::vector<std::byte> data;
            auto                   enc = make_encoder(data);
            REQUIRE(enc(std::vector<std::byte>(64, std::byte{0xAB})));

            // Leave room for MSVC Debug STL container proxy state so the bounded
            // resource fails on the payload reserve.
            check_decode_out_of_memory<std::pmr::vector<std::byte>, 32>(data);
        }
    }

    TEST_CASE("Decode bounded pmr binary string failure preserves target") {
        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);
        REQUIRE(enc(std::vector<std::byte>(64, std::byte{0xAB})));

        // MSVC's Debug STL needs room for container proxy state before the
        // intended payload allocation failure.
        std::array<std::byte, 40>           storage{};
        std::pmr::monotonic_buffer_resource resource(storage.data(), storage.size(), std::pmr::null_memory_resource());
        const auto                          original = std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
        std::pmr::vector<std::byte>         decoded(original.begin(), original.end(), &resource);

        auto dec    = make_decoder(data);
        auto result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::out_of_memory);
        CHECK_EQ(decoded.size(), original.size());
        CHECK(std::ranges::equal(decoded, original));
    }

    TEST_CASE("Decode pmr strings use target resource instead of default resource") {
        {
            const auto             source = std::string(64, 'x');
            std::vector<std::byte> data;
            auto                   enc = make_encoder(data);
            REQUIRE(enc(source));

            std::array<std::byte, 4096>         storage{};
            std::pmr::monotonic_buffer_resource resource(storage.data(), storage.size(), std::pmr::null_memory_resource());
            std::pmr::string                    decoded(&resource);
            throwing_memory_resource            throwing_default;

            {
                default_memory_resource_guard guard(&throwing_default);
                auto                          dec    = make_decoder(data);
                auto                          result = dec(decoded);
                if (!result) {
                    CBOR_TAGS_TEST_LOG("Error: {}\n", status_message(result.error()));
                }
                REQUIRE(result);
            }

            CHECK_EQ(std::string_view(decoded.data(), decoded.size()), std::string_view(source));
        }

        {
            const auto             source = std::vector<std::byte>(64, std::byte{0xAB});
            std::vector<std::byte> data;
            auto                   enc = make_encoder(data);
            REQUIRE(enc(source));

            std::array<std::byte, 4096>         storage{};
            std::pmr::monotonic_buffer_resource resource(storage.data(), storage.size(), std::pmr::null_memory_resource());
            std::pmr::vector<std::byte>         decoded(&resource);
            throwing_memory_resource            throwing_default;

            {
                default_memory_resource_guard guard(&throwing_default);
                auto                          dec    = make_decoder(data);
                auto                          result = dec(decoded);
                if (!result) {
                    CBOR_TAGS_TEST_LOG("Error: {}\n", status_message(result.error()));
                }
                REQUIRE(result);
            }

            CHECK_EQ(decoded.size(), source.size());
            CHECK(std::ranges::equal(decoded, source));
        }
    }

    TEST_CASE("Decode nested pmr arrays use parent resource instead of default resource") {
        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);
        REQUIRE(enc(std::vector<std::vector<int>>{{1, 2, 3}, {4, 5}}));

        std::array<std::byte, 4096>             storage{};
        std::pmr::monotonic_buffer_resource     resource(storage.data(), storage.size(), std::pmr::null_memory_resource());
        std::pmr::vector<std::pmr::vector<int>> decoded(&resource);
        throwing_memory_resource                throwing_default;

        {
            default_memory_resource_guard guard(&throwing_default);
            auto                          dec    = make_decoder(data);
            auto                          result = dec(decoded);
            if (!result) {
                CBOR_TAGS_TEST_LOG("Error: {}\n", status_message(result.error()));
            }
            REQUIRE(result);
        }

        REQUIRE_EQ(decoded.size(), 2);
        REQUIRE_EQ(decoded[0].size(), 3);
        CHECK_EQ(decoded[0][0], 1);
        CHECK_EQ(decoded[0][1], 2);
        CHECK_EQ(decoded[0][2], 3);
        REQUIRE_EQ(decoded[1].size(), 2);
        CHECK_EQ(decoded[1][0], 4);
        CHECK_EQ(decoded[1][1], 5);
    }

    TEST_CASE("Decode nested pmr maps and mapped arrays use parent resource instead of default resource") {
        {
            std::vector<std::byte> data;
            auto                   enc = make_encoder(data);
            REQUIRE(enc(std::vector<std::map<int, int>>{{{1, 2}}}));

            std::array<std::byte, 4096>               storage{};
            std::pmr::monotonic_buffer_resource       resource(storage.data(), storage.size(), std::pmr::null_memory_resource());
            std::pmr::vector<std::pmr::map<int, int>> decoded(&resource);
            throwing_memory_resource                  throwing_default;

            {
                default_memory_resource_guard guard(&throwing_default);
                auto                          dec    = make_decoder(data);
                auto                          result = dec(decoded);
                if (!result) {
                    CBOR_TAGS_TEST_LOG("Error: {}\n", status_message(result.error()));
                }
                REQUIRE(result);
            }

            REQUIRE_EQ(decoded.size(), 1);
            CHECK_EQ(decoded[0].size(), 1);
            CHECK_EQ(decoded[0].begin()->first, 1);
            CHECK_EQ(decoded[0].begin()->second, 2);
        }

        {
            std::vector<std::byte> data;
            auto                   enc = make_encoder(data);
            REQUIRE(enc(std::map<int, std::vector<int>>{{1, {2, 3, 4}}}));

            std::array<std::byte, 4096>               storage{};
            std::pmr::monotonic_buffer_resource       resource(storage.data(), storage.size(), std::pmr::null_memory_resource());
            std::pmr::map<int, std::pmr::vector<int>> decoded(&resource);
            throwing_memory_resource                  throwing_default;

            {
                default_memory_resource_guard guard(&throwing_default);
                auto                          dec    = make_decoder(data);
                auto                          result = dec(decoded);
                if (!result) {
                    CBOR_TAGS_TEST_LOG("Error: {}\n", status_message(result.error()));
                }
                REQUIRE(result);
            }

            REQUIRE_EQ(decoded.size(), 1);
            CHECK_EQ(decoded.begin()->first, 1);
            REQUIRE_EQ(decoded.begin()->second.size(), 3);
            CHECK_EQ(decoded.begin()->second[0], 2);
            CHECK_EQ(decoded.begin()->second[1], 3);
            CHECK_EQ(decoded.begin()->second[2], 4);
        }
    }

    TEST_CASE("Decode pmr map string keys and values use parent resource instead of default resource") {
        const auto long_key   = std::string(64, 'k');
        const auto long_value = std::string(64, 'v');

        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);
        REQUIRE(enc(std::map<std::string, std::string>{{long_key, long_value}}));

        std::array<std::byte, 4096>                       storage{};
        std::pmr::monotonic_buffer_resource               resource(storage.data(), storage.size(), std::pmr::null_memory_resource());
        std::pmr::map<std::pmr::string, std::pmr::string> decoded(&resource);
        throwing_memory_resource                          throwing_default;

        {
            default_memory_resource_guard guard(&throwing_default);
            auto                          dec    = make_decoder(data);
            auto                          result = dec(decoded);
            if (!result) {
                CBOR_TAGS_TEST_LOG("Error: {}\n", status_message(result.error()));
            }
            REQUIRE(result);
        }

        REQUIRE_EQ(decoded.size(), 1);
        const auto &entry = *decoded.begin();
        CHECK_EQ(std::string_view(entry.first.data(), entry.first.size()), std::string_view(long_key));
        CHECK_EQ(std::string_view(entry.second.data(), entry.second.size()), std::string_view(long_value));
    }

    TEST_CASE("Decode optional pmr values use parent resource instead of default resource") {
        const auto long_value = std::string(64, 'o');

        {
            std::vector<std::byte> data;
            auto                   enc = make_encoder(data);
            REQUIRE(enc(std::vector<std::optional<std::string>>{long_value, std::nullopt}));

            std::array<std::byte, 4096>                       storage{};
            std::pmr::monotonic_buffer_resource               resource(storage.data(), storage.size(), std::pmr::null_memory_resource());
            std::pmr::vector<std::optional<std::pmr::string>> decoded(&resource);
            throwing_memory_resource                          throwing_default;

            {
                default_memory_resource_guard guard(&throwing_default);
                auto                          dec    = make_decoder(data);
                auto                          result = dec(decoded);
                if (!result) {
                    CBOR_TAGS_TEST_LOG("Error: {}\n", status_message(result.error()));
                }
                REQUIRE(result);
            }

            REQUIRE_EQ(decoded.size(), 2);
            REQUIRE(decoded[0].has_value());
            CHECK_EQ(std::string_view(decoded[0]->data(), decoded[0]->size()), std::string_view(long_value));
            CHECK_FALSE(decoded[1].has_value());
        }

        {
            std::vector<std::byte> data;
            auto                   enc = make_encoder(data);
            REQUIRE(enc(std::map<int, std::optional<std::string>>{{1, long_value}, {2, std::nullopt}}));

            std::array<std::byte, 4096>                         storage{};
            std::pmr::monotonic_buffer_resource                 resource(storage.data(), storage.size(), std::pmr::null_memory_resource());
            std::pmr::map<int, std::optional<std::pmr::string>> decoded(&resource);
            throwing_memory_resource                            throwing_default;

            {
                default_memory_resource_guard guard(&throwing_default);
                auto                          dec    = make_decoder(data);
                auto                          result = dec(decoded);
                if (!result) {
                    CBOR_TAGS_TEST_LOG("Error: {}\n", status_message(result.error()));
                }
                REQUIRE(result);
            }

            REQUIRE_EQ(decoded.size(), 2);
            REQUIRE(decoded.at(1).has_value());
            CHECK_EQ(std::string_view(decoded.at(1)->data(), decoded.at(1)->size()), std::string_view(long_value));
            CHECK_FALSE(decoded.at(2).has_value());
        }
    }

    TEST_CASE("Decode wrong major type in variant") {
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        REQUIRE(enc(140_tag, "Hello world!"sv));

        auto dec    = make_decoder(data);
        auto result = dec(std::variant<int, float, double, bool, std::nullptr_t>{});
        REQUIRE(!result);
        CHECK_EQ(result.error(), status_code::no_match_in_variant_on_buffer);
    }

    TEST_CASE("Decode wrong major type in variant, with matching types") {
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        REQUIRE(enc(140_tag, "Hello world!"sv));

        {
            auto dec    = make_decoder(data);
            auto result = dec(std::variant<std::pair<static_tag<139>, std::string>, std::pair<static_tag<141>, std::string>>{});
            REQUIRE(!result);
            CHECK_EQ(result.error(), status_code::no_match_in_variant_on_buffer);
        }

        { /* Sanity check with matching tag valu in variant */
            std::variant<std::pair<static_tag<139>, std::string>, std::pair<static_tag<140>, std::string>> variant;
            auto                                                                                           dec     = make_decoder(data);
            auto                                                                                           result2 = dec(variant);
            CHECK_MESSAGE(result2, "Error: {}\n", status_message(result2.error()));
            CHECK(std::holds_alternative<std::pair<static_tag<140>, std::string>>(variant));
        }
    }

    TEST_CASE("Decode dynamic tag, with wrong value") {
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        REQUIRE(enc(dynamic_tag<uint64_t>{140}, "Hello world!"sv));

        auto dec    = make_decoder(data);
        auto result = dec(dynamic_tag<uint64_t>{141}, std::string{});
        REQUIRE(!result);
        CHECK_EQ(result.error(), status_code::no_match_for_tag);
    }
}

TEST_SUITE("Open objects - wrap as etc") {
    TEST_CASE("Basic") {
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        REQUIRE(enc(140_tag, wrap_as_array{1, 2}));

        CBOR_TAGS_TEST_LOG("data: {}\n", to_hex(data));

        auto dec = make_decoder(data);
        int  a, b;
        auto c      = wrap_as_array{a, b};
        auto result = dec(140_tag, c);
        REQUIRE(result);
        CHECK_EQ(a, 1);
        CHECK_EQ(b, 2);
    }

    TEST_CASE("Reject non-array header") {
        std::vector<std::byte> data{std::byte{0xA0}}; // map(0), not array(0)

        auto dec = make_decoder(data);

        int  a{};
        int  b{};
        auto result = dec(wrap_as_array{a, b});

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
    }
}

TEST_SUITE("Views errors") {
    TEST_CASE_TEMPLATE("decode contiguous view on non-contiguous data [bstr]", T, std::span<const std::byte>,
                       std::basic_string_view<std::byte>) {
        auto data = std::deque<std::byte>{};
        auto enc  = make_encoder(data);
        auto bstr = std::basic_string<std::byte>{static_cast<std::byte>('a'), static_cast<std::byte>('b')};
        REQUIRE(enc(bstr));

        auto dec    = make_decoder(data);
        auto view   = T{};
        auto result = dec(view);
        REQUIRE_FALSE(result);
        INFO("Error: " << status_message(result.error()));
        CHECK_EQ(result.error(), status_code::contiguous_view_on_non_contiguous_data);
    }

    TEST_CASE_TEMPLATE("decode contiguous view on non-contiguous data [tstr]", T, std::string_view) {
        auto        data = std::deque<std::byte>{};
        auto        enc  = make_encoder(data);
        std::string str{"hello"};
        REQUIRE(enc(str));

        auto dec    = make_decoder(data);
        auto view   = T{};
        auto result = dec(view);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::contiguous_view_on_non_contiguous_data);
    }
}
