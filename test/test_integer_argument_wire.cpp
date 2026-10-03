#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/detail/cbor_argument.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <iterator>
#include <ranges>
#include <span>
#include <type_traits>
#include <vector>

using namespace cbor::tags;

namespace {

struct argument_fixture {
    std::array<std::uint8_t, 10> bytes;
    std::size_t                  payload_size;
    std::uint64_t                value;
};

constexpr std::array argument_fixtures{
    argument_fixture{{0x18, 0xa5, 0x07}, 1, 0xa5},
    argument_fixture{{0x19, 0x81, 0x23, 0x07}, 2, 0x8123},
    argument_fixture{{0x1a, 0x89, 0xab, 0xcd, 0xef, 0x07}, 4, 0x89abcdef},
    argument_fixture{{0x1b, 0x81, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x07}, 8, 0x8123456789abcdefULL},
};

constexpr std::array constexpr_payload{std::byte{0x81}, std::byte{0x23}, std::byte{0x45}, std::byte{0x67},
                                       std::byte{0x89}, std::byte{0xab}, std::byte{0xcd}, std::byte{0xef}};
static_assert(detail::load_cbor_big_endian<std::uint8_t>(constexpr_payload.data()) == 0x81);
static_assert(detail::load_cbor_big_endian<std::uint16_t>(constexpr_payload.data()) == 0x8123);
static_assert(detail::load_cbor_big_endian<std::uint32_t>(constexpr_payload.data()) == 0x81234567);
static_assert(detail::load_cbor_big_endian<std::uint64_t>(constexpr_payload.data()) == 0x8123456789abcdefULL);

struct counting_unsized_argument_range {
    struct iterator {
        using value_type        = std::byte;
        using difference_type   = std::ptrdiff_t;
        using iterator_concept  = std::bidirectional_iterator_tag;
        using iterator_category = std::bidirectional_iterator_tag;

        const std::byte *current{};
        std::size_t     *increments{};

        const std::byte &operator*() const noexcept { return *current; }
        iterator        &operator++() noexcept {
            ++current;
            ++*increments;
            return *this;
        }
        iterator operator++(int) noexcept {
            auto copy = *this;
            ++*this;
            return copy;
        }
        iterator &operator--() noexcept {
            --current;
            return *this;
        }
        iterator operator--(int) noexcept {
            auto copy = *this;
            --*this;
            return copy;
        }
        friend bool operator==(const iterator &, const iterator &) = default;
    };

    std::span<const std::byte> bytes;
    mutable std::size_t        increments{};

    iterator begin() const noexcept { return {bytes.data(), &increments}; }
    iterator end() const noexcept { return {bytes.data() + bytes.size(), &increments}; }
};

static_assert(CborInputBuffer<counting_unsized_argument_range>);
static_assert(!std::ranges::sized_range<const counting_unsized_argument_range>);

template <typename Self> struct argument_tag_observer : decoder_mixin_base<Self> {
    using decoder_mixin_base<Self>::decode;

    std::vector<std::uint64_t> tags;
    status_code                result = status_code::success;

    status_code observe_decoded_cbor_tag(std::uint64_t tag) {
        tags.push_back(tag);
        return result;
    }
};

} // namespace

TEST_SUITE("cbor_wire/integer_arguments") {

    TEST_CASE("fixed integer argument widths decode at every alignment and leave the following item") {
        const auto check = []<typename Byte>() {
            for (const auto &fixture : argument_fixtures) {
                for (std::size_t offset = 0; offset < 8; ++offset) {
                    CAPTURE(fixture.payload_size);
                    CAPTURE(offset);
                    alignas(std::uint64_t) std::array<Byte, 18> storage{};
                    for (std::size_t index = 0; index < fixture.payload_size + 2; ++index) {
                        storage[offset + index] = static_cast<Byte>(fixture.bytes[index]);
                    }
                    const std::span<const Byte> input{storage.data() + offset, fixture.payload_size + 2};
                    auto                        dec = make_decoder(input);
                    std::uint64_t               value{};
                    REQUIRE(dec(value));
                    CHECK_EQ(value, fixture.value);
                    CHECK(dec.tell() == input.begin() + static_cast<std::ptrdiff_t>(fixture.payload_size + 1));
                    REQUIRE(dec(value));
                    CHECK_EQ(value, 7);
                    CHECK(dec.tell() == input.end());
                }
            }
        };
        check.template operator()<std::byte>();
        check.template operator()<char>();
        check.template operator()<signed char>();
        check.template operator()<unsigned char>();
        check.template operator()<volatile std::byte>();
        check.template operator()<volatile unsigned char>();
    }

    TEST_CASE("every truncated integer argument prefix preserves the checked contiguous payload cursor") {
        for (const auto &fixture : argument_fixtures) {
            std::array<std::byte, 10> storage{};
            for (std::size_t index = 0; index < fixture.bytes.size(); ++index) {
                storage[index] = static_cast<std::byte>(fixture.bytes[index]);
            }
            // Bytes beyond the admitted span exist, so the boundary is the
            // supplied slice rather than the backing allocation.
            for (std::size_t size = 0; size < fixture.payload_size + 1; ++size) {
                CAPTURE(fixture.payload_size);
                CAPTURE(size);
                const std::span<const std::byte> input{storage.data(), size};
                auto                             dec    = make_decoder(input);
                std::uint64_t                    value  = 99;
                const auto                       result = dec(value);
                REQUIRE_FALSE(result);
                CHECK_EQ(result.error(), status_code::incomplete);
                CHECK_EQ(value, 99);
                CHECK(dec.tell() == input.begin() + (size == 0 ? 0 : 1));
            }
            for (std::size_t size = 0; size < fixture.payload_size; ++size) {
                const std::span<const std::byte> payload{storage.data() + 1, size};
                auto                             dec = make_decoder(payload);
                CHECK_THROWS_AS(dec.decode_unsigned(static_cast<std::byte>(fixture.bytes[0])), parse_incomplete_exception);
                CHECK(dec.tell() == payload.begin());
            }
        }
    }

    TEST_CASE("sized and unsized noncontiguous integer arguments retain their terminal consumption rules") {
        for (const auto &fixture : argument_fixtures) {
            std::array<std::byte, 10> storage{};
            for (std::size_t index = 0; index < fixture.bytes.size(); ++index) {
                storage[index] = static_cast<std::byte>(fixture.bytes[index]);
            }
            for (std::size_t size = 0; size < fixture.payload_size + 1; ++size) {
                CAPTURE(fixture.payload_size);
                CAPTURE(size);
                const std::deque<std::byte> sized{storage.begin(), storage.begin() + static_cast<std::ptrdiff_t>(size)};
                auto                        sized_dec    = make_decoder(sized);
                std::uint64_t               value        = 99;
                const auto                  sized_result = sized_dec(value);
                REQUIRE_FALSE(sized_result);
                CHECK_EQ(sized_result.error(), status_code::incomplete);
                CHECK_EQ(value, 99);
                CHECK(sized_dec.tell() == sized.begin() + (size == 0 ? 0 : 1));

                const counting_unsized_argument_range unsized{{storage.data(), size}};
                auto                                  unsized_dec    = make_decoder(unsized);
                const auto                            unsized_result = unsized_dec(value);
                REQUIRE_FALSE(unsized_result);
                CHECK_EQ(unsized_result.error(), status_code::incomplete);
                CHECK_EQ(value, 99);
                CHECK(unsized_dec.tell() == unsized.end());
                CHECK_EQ(unsized.increments, size);
            }
            const counting_unsized_argument_range input{{storage.data(), fixture.payload_size + 2}};
            auto                                  dec = make_decoder(input);
            std::uint64_t                         value{};
            REQUIRE(dec(value));
            CHECK_EQ(value, fixture.value);
            CHECK_EQ(input.increments, fixture.payload_size + 1);
            CHECK(dec.tell().current == storage.data() + fixture.payload_size + 1);
            REQUIRE(dec(value));
            CHECK_EQ(value, 7);
            CHECK_EQ(input.increments, fixture.payload_size + 2);
        }
    }

    TEST_CASE("reserved integer argument information fails before reading a payload") {
        for (std::uint8_t info = 28; info < 32; ++info) {
            const std::array input{static_cast<std::byte>(info), std::byte{0x07}};
            auto             dec    = make_decoder(input);
            std::uint64_t    value  = 99;
            const auto       result = dec(value);
            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), status_code::invalid_additional_info);
            CHECK_EQ(value, 99);
            CHECK(dec.tell() == input.begin() + 1);

            auto direct_dec = make_decoder(input);
            CHECK_THROWS_AS(direct_dec.decode_unsigned(static_cast<std::byte>(info)), detail::decode_status_exception);
            CHECK(direct_dec.tell() == input.begin());
        }
    }

    TEST_CASE("fixed width tag arguments invoke the observer once and propagate its status") {
        for (const auto &fixture : argument_fixtures) {
            std::array<std::byte, 10> storage{};
            for (std::size_t index = 0; index < fixture.payload_size + 2; ++index) {
                storage[index] = static_cast<std::byte>(fixture.bytes[index]);
            }
            storage[0] |= std::byte{0xc0};
            const std::span<const std::byte> input{storage.data(), fixture.payload_size + 2};
            auto                             dec = make_decoder<argument_tag_observer>(input);
            as_tag_any                       tag;
            REQUIRE(dec(tag));
            CHECK_EQ(tag.tag, fixture.value);
            CHECK(dec.tags == std::vector<std::uint64_t>{fixture.value});
            CHECK(dec.tell() == input.begin() + static_cast<std::ptrdiff_t>(fixture.payload_size + 1));

            auto rejected_dec   = make_decoder<argument_tag_observer>(input);
            rejected_dec.result = status_code::no_match_for_tag;
            const auto result   = rejected_dec(tag);
            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), status_code::no_match_for_tag);
            CHECK(rejected_dec.tags == std::vector<std::uint64_t>{fixture.value});
            CHECK(rejected_dec.tell() == input.begin() + static_cast<std::ptrdiff_t>(fixture.payload_size + 1));
        }
    }

} // TEST_SUITE("cbor_wire/integer_arguments")
