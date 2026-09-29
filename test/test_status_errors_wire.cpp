#include "test_util.h"

#include <cbor_tags/cbor_extensions.h>
#include <cbor_tags/cbor_traversal.h>
#include <cbor_tags/detail/cbor_extension_decode.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <list>
#include <new>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace cbor::tags;

namespace {

struct wire_error_case {
    std::string_view hex;
    status_code      status;
};

template <typename Fn> void with_input_ranges(const std::vector<std::byte> &bytes, Fn check) {
    check(bytes);
    check(std::deque<std::byte>{bytes.begin(), bytes.end()});
    const std::list<std::byte> storage(bytes.begin(), bytes.end());
    const auto                 unsized = std::ranges::subrange(storage.begin(), storage.end());
    static_assert(!std::ranges::sized_range<decltype(unsized)>);
    check(unsized);
}

template <std::uint64_t Tag> struct tagged_bytes {
    static constexpr std::uint64_t cbor_tag = Tag;
    std::vector<std::byte>         value;
};

template <typename Self> struct observing_codec : decoder_mixin_base<Self> {
    using decoder_mixin_base<Self>::decode;
    std::vector<std::uint64_t> attempts;

    template <std::uint64_t Tag> status_code decode(tagged_bytes<Tag> &value, std::uint64_t actual_tag) {
        attempts.push_back(Tag);
        if (actual_tag != Tag) {
            return status_code::no_match_for_tag;
        }
        return static_cast<Self &>(*this).applier(value.value);
    }
};

enum class codec_failure { standard, allocation, length, returned_status };

struct failing_value {
    codec_failure failure;

    template <typename Decoder> expected<void, status_code> decode(Decoder &) {
        switch (failure) {
        // Matching a library message must not change an application's exception classification.
        case codec_failure::standard: throw std::runtime_error{"Invalid CBOR additional information"};
        case codec_failure::allocation: throw std::bad_alloc{};
        case codec_failure::length: throw std::length_error{"application length limit"};
        case codec_failure::returned_status: return cbor::tags::unexpected<status_code>{status_code::unsupported_operation};
        }
        return cbor::tags::unexpected<status_code>{status_code::error};
    }
};

} // namespace

TEST_SUITE("cbor_wire/status_errors") {

    TEST_CASE("reserved additional information is preserved by raw views and traversal for every major") {
        for (unsigned major = 0; major < 8; ++major) {
            for (unsigned info = 28; info < 31; ++info) {
                CAPTURE(major);
                CAPTURE(info);
                with_input_ranges({static_cast<std::byte>((major << 5U) | info)}, [](const auto &input) {
                    auto                                          dec = make_decoder(input);
                    typename decltype(dec)::raw_encoded_item_view value;
                    const auto                                    result = dec(value);
                    REQUIRE_FALSE(result);
                    CHECK_EQ(result.error(), status_code::invalid_additional_info);

                    auto       validation_dec = make_decoder(input);
                    const auto validation     = validate_item(validation_dec);
                    REQUIRE_FALSE(validation);
                    CHECK_EQ(validation.error(), status_code::invalid_additional_info);
                });
            }
        }
    }

    TEST_CASE("typed and extension argument readers distinguish invalid encodings from truncation") {
        for (const auto *hex : {"1c", "1d", "1e", "1f"}) {
            CAPTURE(std::string_view{hex});
            with_input_ranges(to_bytes(hex), [](const auto &input) {
                std::uint64_t value  = 99;
                auto          dec    = make_decoder(input);
                const auto    result = dec(value);
                REQUIRE_FALSE(result);
                CHECK_EQ(result.error(), status_code::invalid_additional_info);
                CHECK_EQ(value, 99);
                CHECK(dec.tell() == input.end());

                auto extension_dec       = make_decoder(input);
                const auto [major, info] = extension_dec.read_initial_byte();
                CHECK_EQ(major, major_type::UnsignedInteger);
                CHECK_EQ(detail::decode_unsigned_argument(extension_dec, info, value), status_code::invalid_additional_info);
            });
        }
        for (const auto *hex : {"18", "1900", "1a000000", "1b00000000000000"}) {
            CAPTURE(std::string_view{hex});
            with_input_ranges(to_bytes(hex), [](const auto &input) {
                std::uint64_t value  = 99;
                auto          dec    = make_decoder(input);
                const auto    result = dec(value);
                REQUIRE_FALSE(result);
                CHECK_EQ(result.error(), status_code::incomplete);
                CHECK_EQ(value, 99);
                CHECK(dec.tell() != input.begin());
            });
        }
    }

    TEST_CASE("malformed indefinite strings keep completed chunks and their terminal cursor") {
        with_input_ranges(to_bytes("7f626f6b4100ff"), [](const auto &input) {
            std::string value;
            auto        dec    = make_decoder(input);
            const auto  result = dec(value);
            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), status_code::malformed_structure);
            CHECK_EQ(value, "ok");
            CHECK_EQ(std::ranges::distance(input.begin(), dec.tell()), 5);
        });

        const auto                 bytes = to_bytes("636f6b");
        const std::list<std::byte> storage(bytes.begin(), bytes.end());
        const auto                 unsized = std::ranges::subrange(storage.begin(), storage.end());
        std::string                value;
        auto                       dec    = make_decoder(unsized);
        const auto                 result = dec(value);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
        CHECK_EQ(value, "ok");
        CHECK(dec.tell() == unsized.end());
    }

    TEST_CASE("fixed headers distinguish wrong major size and invalid arguments") {
        constexpr wire_error_case cases[]{
            {"a0", status_code::no_match_for_array_on_buffer},
            {"81", status_code::unexpected_group_size},
            {"9f", status_code::unexpected_group_size},
            {"9c", status_code::invalid_additional_info},
            {"99", status_code::incomplete},
        };

        for (const auto &[hex, expected_status] : cases) {
            CAPTURE(hex);
            with_input_ranges(to_bytes(hex), [&](const auto &input) {
                auto       dec    = make_decoder(input);
                const auto result = dec(as_array{0});
                REQUIRE_FALSE(result);
                CHECK_EQ(result.error(), expected_status);
            });
        }
        const auto input  = to_bytes("80");
        auto       dec    = make_decoder(input);
        const auto result = dec(as_map{0});
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_map_on_buffer);
    }

    TEST_CASE("variants stop after malformed tagged payloads and retry genuine tag mismatches") {
        using value_type = std::variant<tagged_bytes<42>, tagged_bytes<43>>;
        constexpr wire_error_case cases[]{
            {"d82a5c", status_code::invalid_additional_info},
            {"d82a5f6100ff", status_code::malformed_structure},
        };

        for (const auto &[hex, expected_status] : cases) {
            CAPTURE(hex);
            with_input_ranges(to_bytes(hex), [&](const auto &input) {
                value_type value{tagged_bytes<43>{{std::byte{7}}}};
                auto       dec    = make_decoder<observing_codec>(input);
                const auto result = dec(value);
                REQUIRE_FALSE(result);
                CHECK_EQ(result.error(), expected_status);
                CHECK_EQ(dec.attempts, (std::vector<std::uint64_t>{42}));
                REQUIRE_EQ(value.index(), 1);
                CHECK_EQ(std::get<1>(value).value, (std::vector<std::byte>{std::byte{7}}));
            });
        }
        with_input_ranges(to_bytes("d82b4109"), [](const auto &input) {
            value_type value;
            auto       dec = make_decoder<observing_codec>(input);
            REQUIRE(dec(value));
            CHECK_EQ(dec.attempts, (std::vector<std::uint64_t>{42, 43}));
            REQUIRE_EQ(value.index(), 1);
            CHECK_EQ(std::get<1>(value).value, (std::vector<std::byte>{std::byte{9}}));
            CHECK(dec.tell() == input.end());
        });
    }

    TEST_CASE("raw scanners distinguish unrepresentable counts and truncated containers") {
        constexpr wire_error_case cases[]{
            {"bbffffffffffffffff", status_code::size_limit_exceeded},
            {"bb0000000000000001", status_code::incomplete},
        };

        for (const auto &[hex, expected_status] : cases) {
            CAPTURE(hex);
            with_input_ranges(to_bytes(hex), [&](const auto &input) {
                auto                                         dec = make_decoder(input);
                typename decltype(dec)::raw_encoded_map_view value;
                const auto                                   result = dec(value);
                REQUIRE_FALSE(result);
                CHECK_EQ(result.error(), expected_status);
            });
        }

        // Exercise a small representable extent without allocating a huge buffer.
        auto input = to_bytes("590100");
        input.resize(259, std::byte{0});
        detail::raw_encoded_item_bounds<decltype(input.cbegin()), std::uint8_t> bounds;
        const auto status = detail::read_raw_encoded_item_bounds<decltype(input), std::uint8_t>(input, input.cbegin(), std::nullopt,
                                                                                                status_code::error, bounds);
        CHECK_EQ(status, status_code::size_limit_exceeded);
    }

    TEST_CASE("customization failures keep allocation and unknown exception classifications") {
        struct failure_case {
            codec_failure failure;
            status_code   status;
        };
        constexpr failure_case cases[]{
            {codec_failure::standard, status_code::error},
            {codec_failure::allocation, status_code::out_of_memory},
            {codec_failure::length, status_code::out_of_memory},
            {codec_failure::returned_status, status_code::unsupported_operation},
        };

        const auto input = to_bytes("00");
        for (const auto &[failure, expected_status] : cases) {
            CAPTURE(failure);
            failing_value value{failure};
            auto          dec    = make_decoder(input);
            const auto    result = dec(value);
            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), expected_status);
        }
    }
}
