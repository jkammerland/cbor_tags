#include "test_util.h"

#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_segments.h>
#include <cbor_tags/codec/typed_array.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <list>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using namespace cbor::tags;

namespace {

using count_options        = Options<Option<expected<std::size_t, status_code>>, default_wrapping>;
using legacy_count_options = Options<Option<expected<std::uint64_t, status_code>>, default_wrapping>;

struct status_result {
    using error_type = status_code;

    status_code status{status_code::success};
    status_result() = default;
    status_result(cbor::tags::unexpected<status_code> failure) : status(failure.error()) {}

    bool        has_value() const noexcept { return status == status_code::success; }
    explicit    operator bool() const noexcept { return has_value(); }
    status_code error() const noexcept { return status; }
};

struct status_options {
    using is_options                  = void;
    using return_type                 = status_result;
    static constexpr bool wrap_groups = true;
};

static_assert(std::same_as<count_options::return_type, expected<std::size_t, status_code>>);
static_assert(std::same_as<legacy_count_options::return_type, expected<std::uint64_t, status_code>>);
static_assert(detail::returns_byte_count_v<count_options::return_type>);
static_assert(detail::returns_byte_count_v<legacy_count_options::return_type>);
static_assert(!detail::returns_byte_count_v<status_result>);
static_assert(std::same_as<default_options::return_type, expected<void, status_code>>);

struct measured_output {
    using value_type     = std::byte;
    using size_type      = std::size_t;
    using iterator       = std::list<std::byte>::iterator;
    using const_iterator = std::list<std::byte>::const_iterator;

    std::list<std::byte> bytes;
    mutable std::size_t  size_calls{};
    std::size_t          throw_on_size_call{};

    void                              push_back(std::byte value) { bytes.push_back(value); }
    template <typename Iterator> auto insert(const_iterator position, Iterator first, Iterator last) {
        return bytes.insert(position, first, last);
    }
    auto insert(const_iterator position, std::initializer_list<std::byte> values) { return bytes.insert(position, values); }

    auto begin() noexcept { return bytes.begin(); }
    auto end() noexcept { return bytes.end(); }
    auto begin() const noexcept { return bytes.begin(); }
    auto end() const noexcept { return bytes.end(); }
    auto size() const {
        ++size_calls;
        if (size_calls == throw_on_size_call) {
            throw std::runtime_error("size unavailable");
        }
        return bytes.size();
    }
};

struct unsized_output {
    using value_type     = std::byte;
    using size_type      = std::size_t;
    using iterator       = std::list<std::byte>::iterator;
    using const_iterator = std::list<std::byte>::const_iterator;

    std::list<std::byte> bytes;

    void                              push_back(std::byte value) { bytes.push_back(value); }
    template <typename Iterator> auto insert(const_iterator position, Iterator first, Iterator last) {
        return bytes.insert(position, first, last);
    }
    auto insert(const_iterator position, std::initializer_list<std::byte> values) { return bytes.insert(position, values); }
    auto begin() noexcept { return bytes.begin(); }
    auto end() noexcept { return bytes.end(); }
    auto begin() const noexcept { return bytes.begin(); }
    auto end() const noexcept { return bytes.end(); }
};

static_assert(CborOutputBuffer<measured_output>);
static_assert(CborOutputBuffer<unsized_output>);
static_assert(!std::ranges::sized_range<unsized_output>);

struct counted_unsized_input {
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

    auto begin() const noexcept { return iterator{bytes.data(), &increments}; }
    auto end() const noexcept { return iterator{bytes.data() + bytes.size(), &increments}; }
};

static_assert(CborInputBuffer<counted_unsized_input>);
static_assert(!std::ranges::sized_range<const counted_unsized_input>);

struct nested_value {
    unsigned            first{24};
    unsigned            second{1};
    mutable std::size_t header_count{};
    mutable std::size_t first_count{};
    mutable std::size_t second_count{};

    template <typename Encoder> auto encode(Encoder &enc) const {
        auto header = enc(as_array{2});
        if (!header) {
            return header;
        }
        header_count = *header;
        auto left    = enc(first);
        if (!left) {
            return left;
        }
        first_count = *left;
        auto right  = enc(second);
        if (right) {
            second_count = *right;
        }
        return right;
    }

    template <typename Decoder> auto decode(Decoder &dec) {
        auto header = dec(as_array{2});
        if (!header) {
            return header;
        }
        header_count = *header;
        auto left    = dec(first);
        if (!left) {
            return left;
        }
        first_count = *left;
        auto right  = dec(second);
        if (right) {
            second_count = *right;
        }
        return right;
    }
};

struct failing_value {
    unsigned decoded{};

    template <typename Encoder> auto encode(Encoder &enc) const {
        auto prefix = enc(1U);
        if (!prefix) {
            return prefix;
        }
        return decltype(prefix){cbor::tags::unexpected<status_code>{status_code::size_limit_exceeded}};
    }

    template <typename Decoder> auto decode(Decoder &dec) {
        auto prefix = dec(decoded);
        if (!prefix) {
            return prefix;
        }
        return decltype(prefix){cbor::tags::unexpected<status_code>{status_code::size_limit_exceeded}};
    }
};

struct direct_value {
    unsigned number;

    // Keep this value out of the decoder's aggregate-reflection overload.
    explicit direct_value(unsigned value = 24) noexcept : number(value) {}
};

static_assert(!IsAggregate<direct_value>);

template <typename Self> struct direct_codec : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;

    void encode(const direct_value &value) {
        auto &enc = static_cast<Self &>(*this);
        enc.encode(static_tag<100>{});
        enc.encode(value.number);
    }

    status_code decode(direct_value &value, major_type major, std::byte info) {
        auto           &dec = static_cast<Self &>(*this);
        static_tag<100> tag;
        const auto      status = dec.decode(tag, major, info);
        if (status != status_code::success) {
            return status;
        }
        return dec.decode(value.number);
    }
};

std::uint64_t segmented_wire_size(const byte_segments &segments) {
    std::uint64_t total{};
    for (const auto &segment : segments) {
        total += static_cast<std::uint64_t>(segment.size());
    }
    return total;
}

} // namespace

TEST_SUITE("cbor_wire/transcoder_byte_counts") {

    TEST_CASE_TEMPLATE("byte counts measure each call and all arguments", Options, count_options, legacy_count_options) {
        auto       output = to_bytes("ff");
        auto       enc    = make_encoder_with_options<Options>(output);
        const auto first  = enc(1U, 24U);
        REQUIRE(first);
        CHECK_EQ(*first, 3U);
        const auto tagged = enc(static_tag<100>{}, std::string_view{"hi"});
        REQUIRE(tagged);
        CHECK_EQ(*tagged, 5U);
        const auto empty = enc();
        REQUIRE(empty);
        CHECK_EQ(*empty, 0U);
        CHECK_EQ(to_hex(output), "ff011818d864626869");

        const auto input = to_bytes("011818d86462686907");
        auto       dec   = make_decoder_with_options<Options>(input);
        unsigned   one{}, twenty_four{};
        const auto decoded = dec(one, twenty_four);
        REQUIRE(decoded);
        CHECK_EQ(*decoded, 3U);
        CHECK_EQ(one, 1U);
        CHECK_EQ(twenty_four, 24U);
        static_tag<100> tag;
        std::string     text;
        const auto      decoded_tag = dec(tag, text);
        REQUIRE(decoded_tag);
        CHECK_EQ(*decoded_tag, 5U);
        CHECK_EQ(text, "hi");
        const auto decoded_empty = dec();
        REQUIRE(decoded_empty);
        CHECK_EQ(*decoded_empty, 0U);
        unsigned   following{};
        const auto last = dec(following);
        REQUIRE(last);
        CHECK_EQ(*last, 1U);
        CHECK_EQ(following, 7U);
    }

    TEST_CASE("byte counts use append size and fixed output cursor positions") {
        std::deque<std::byte> output{std::byte{0xff}};
        auto                  enc    = make_encoder_with_options<count_options>(output);
        const auto            result = enc(1U, 24U);
        REQUIRE(result);
        CHECK_EQ(*result, 3U);
        CHECK_EQ(to_hex(output), "ff011818");

        std::array<std::byte, 8> storage;
        storage.fill(std::byte{0xee});
        auto       span      = std::span{storage};
        auto       fixed_enc = make_encoder_with_options<count_options>(span);
        const auto first     = fixed_enc(1U, 24U);
        const auto second    = fixed_enc(25U);
        REQUIRE(first);
        REQUIRE(second);
        CHECK_EQ(*first, 3U);
        CHECK_EQ(*second, 2U);
        CHECK_EQ(to_hex(storage), "0118181819eeeeee");

        const std::deque<std::byte> input{std::byte{0x01}, std::byte{0x18}, std::byte{0x18}};
        unsigned                    one{}, twenty_four{};
        const auto                  decoded = make_decoder_with_options<count_options>(input)(one, twenty_four);
        REQUIRE(decoded);
        CHECK_EQ(*decoded, 3U);
        CHECK_EQ(one, 1U);
        CHECK_EQ(twenty_four, 24U);
    }

    TEST_CASE("byte segment counts include owned coalescing and borrowed payloads") {
        byte_segments output;
        output.append_owned({std::byte{0xff}});
        auto       enc     = make_encoder_with_options<count_options>(output);
        const auto scalars = enc(1U, 24U);
        REQUIRE(scalars);
        CHECK_EQ(*scalars, 3U);
        REQUIRE_EQ(output.size(), 1U);

        const std::array<std::byte, 2> backing{std::byte{0xaa}, std::byte{0xbb}};
        const auto                     borrowed = enc(std::span<const std::byte>{backing});
        REQUIRE(borrowed);
        CHECK_EQ(*borrowed, 3U);
        const auto following = enc(7U);
        REQUIRE(following);
        CHECK_EQ(*following, 1U);
        CHECK_EQ(to_hex(output.flatten()), "ff01181842aabb07");
        REQUIRE_EQ(output.size(), 3U);
        CHECK(output[0].is_owned());
        CHECK_EQ(output[0].size(), 5U);
        CHECK(output[1].is_borrowed());
        CHECK_EQ(output[1].data(), backing.data());
        CHECK_EQ(output[1].size(), 2U);
        CHECK(output[2].is_owned());
    }

    TEST_CASE("segmented byte counts preserve logical wire lengths beyond four gibibytes") {
        // Reuse one small backing array; only the segment descriptors grow.
        constexpr std::size_t   payload_size  = 65536;
        constexpr std::size_t   payload_count = 65536;
        constexpr std::uint64_t wire_size     = 5ULL + 65536ULL * (5ULL + 65536ULL);
        static_assert(wire_size == 4295294981ULL);
        std::array<std::byte, payload_size> backing{};
        backing.front() = std::byte{0xa5};
        backing.back()  = std::byte{0x5a};
        const std::vector<std::span<const std::byte>> payloads(payload_count, std::span<const std::byte>{backing});

        byte_segments output;
        auto          wide_enc    = make_encoder_with_options<legacy_count_options>(output);
        const auto    wide_result = wide_enc(payloads);
        REQUIRE(wide_result);
        CHECK_EQ(*wide_result, wire_size);
        CHECK_EQ(segmented_wire_size(output), wire_size);

        // Check the independent CBOR headers, order, and borrowing without
        // flattening or visiting the repeated four-gibibyte payload contents.
        const auto initial_header = to_bytes("9a000100005a00010000");
        const auto payload_header = to_bytes("5a00010000");
        REQUIRE_EQ(output.size(), payload_count * 2U);
        bool headers_match  = true;
        bool borrowed_match = true;
        for (std::size_t index = 0; index < output.size(); ++index) {
            const auto &segment = output[index];
            if (index % 2U == 0U) {
                const auto &expected_header = index == 0U ? initial_header : payload_header;
                headers_match               = headers_match && segment.is_owned() && std::ranges::equal(segment.bytes(), expected_header);
            } else {
                borrowed_match = borrowed_match && segment.is_borrowed() && segment.data() == backing.data() &&
                                 segment.size() == backing.size() && segment.bytes().front() == std::byte{0xa5} &&
                                 segment.bytes().back() == std::byte{0x5a};
            }
        }
        CHECK(headers_match);
        CHECK(borrowed_match);

        std::uint64_t expected_total = wire_size;
        if constexpr (std::same_as<count_options, legacy_count_options>) {
            // On ABIs where the result types alias, the first actual call
            // already exercises the size_t option without duplicating metadata.
            const auto native_result = wide_result;
            REQUIRE(native_result);
            CHECK_EQ(*native_result, wire_size);
        } else {
            auto       native_enc    = make_encoder_with_options<count_options>(output);
            const auto native_result = native_enc(payloads);
            if constexpr (std::numeric_limits<std::size_t>::max() < wire_size) {
                REQUIRE_FALSE(native_result);
                CHECK_EQ(native_result.error(), status_code::size_limit_exceeded);
            } else {
                REQUIRE(native_result);
                CHECK_EQ(*native_result, wire_size);
            }
            // A failed count conversion is terminal after the write completes.
            expected_total += wire_size;
            CHECK_EQ(segmented_wire_size(output), expected_total);
        }

        // A large existing total must not narrow the local count of later calls.
        auto       native_enc  = make_encoder_with_options<count_options>(output);
        const auto empty       = native_enc();
        const auto native_tail = native_enc(24U);
        const auto wide_tail   = wide_enc(25U);
        REQUIRE(empty);
        REQUIRE(native_tail);
        REQUIRE(wide_tail);
        CHECK_EQ(*empty, 0U);
        CHECK_EQ(*native_tail, 2U);
        CHECK_EQ(*wide_tail, 2U);

        nested_value nested;
        const auto   nested_result = native_enc(7U, nested, 8U);
        REQUIRE(nested_result);
        CHECK_EQ(*nested_result, 6U);
        CHECK_EQ(nested.header_count, 1U);
        CHECK_EQ(nested.first_count, 2U);
        CHECK_EQ(nested.second_count, 1U);

        REQUIRE(make_encoder(output)(7U));
        CHECK_EQ(segmented_wire_size(output), expected_total + 11U);
    }

    TEST_CASE("byte counts include standalone headers without requiring a payload") {
        std::vector<std::byte> output;
        auto                   enc   = make_encoder_with_options<count_options>(output);
        const auto             array = enc(as_array{24});
        const auto             tag   = enc(static_tag<100>{});
        REQUIRE(array);
        REQUIRE(tag);
        CHECK_EQ(*array, 2U);
        CHECK_EQ(*tag, 2U);
        CHECK_EQ(to_hex(output), "9818d864");

        const auto      input = to_bytes("9818d864");
        auto            dec   = make_decoder_with_options<count_options>(input);
        static_tag<100> decoded_tag;
        const auto      decoded_array  = dec(as_array{24});
        const auto      decoded_header = dec(decoded_tag);
        REQUIRE(decoded_array);
        REQUIRE(decoded_header);
        CHECK_EQ(*decoded_array, 2U);
        CHECK_EQ(*decoded_header, 2U);
    }

    TEST_CASE("nested transcoder calls keep independent count snapshots") {
        nested_value           source;
        std::vector<std::byte> output;
        const auto             encoded = make_encoder_with_options<count_options>(output)(7U, source, 8U);
        REQUIRE(encoded);
        CHECK_EQ(*encoded, 6U);
        CHECK_EQ(source.header_count, 1U);
        CHECK_EQ(source.first_count, 2U);
        CHECK_EQ(source.second_count, 1U);
        CHECK_EQ(to_hex(output), "078218180108");

        const auto   input = to_bytes("078218180108");
        nested_value destination;
        unsigned     before{}, after{};
        const auto   decoded = make_decoder_with_options<count_options>(input)(before, destination, after);
        REQUIRE(decoded);
        CHECK_EQ(*decoded, 6U);
        CHECK_EQ(before, 7U);
        CHECK_EQ(after, 8U);
        CHECK_EQ(destination.first, 24U);
        CHECK_EQ(destination.second, 1U);
        CHECK_EQ(destination.header_count, 1U);
        CHECK_EQ(destination.first_count, 2U);
        CHECK_EQ(destination.second_count, 1U);
    }

    TEST_CASE("byte counts include direct internal extension dispatch") {
        std::vector<std::byte> output;
        const auto             encoded = make_encoder_with_options<count_options, direct_codec>(output)(direct_value{});
        REQUIRE(encoded);
        CHECK_EQ(*encoded, 4U);
        CHECK_EQ(to_hex(output), "d8641818");

        const auto   input = to_bytes("d8641818");
        direct_value destination{0};
        const auto   decoded = make_decoder_with_options<count_options, direct_codec>(input)(destination);
        REQUIRE(decoded);
        CHECK_EQ(*decoded, 4U);
        CHECK_EQ(destination.number, 24U);
    }

    TEST_CASE("byte counts include bulk float and typed array paths") {
        const std::vector<float> values{1.0F, -2.5F};
        std::vector<std::byte>   output;
        output.reserve(64);
        const auto ordinary = make_encoder_with_options<count_options>(output)(values);
        REQUIRE(ordinary);
        CHECK_EQ(*ordinary, 11U);
        CHECK_EQ(to_hex(output), "82fa3f800000fac0200000");

        const auto         ordinary_input = to_bytes("82fa3f800000fac0200000");
        std::vector<float> ordinary_values;
        const auto         ordinary_decoded = make_decoder_with_options<count_options>(ordinary_input)(ordinary_values);
        REQUIRE(ordinary_decoded);
        CHECK_EQ(*ordinary_decoded, 11U);
        CHECK_EQ(ordinary_values, values);

        output.clear();
        auto       enc    = make_encoder_with_options<count_options, codec::typed_array>(output);
        const auto little = enc(rfc8746::as_typed_array(values));
        const auto big    = enc(rfc8746::as_typed_array_be(values));
        REQUIRE(little);
        REQUIRE(big);
        CHECK_EQ(*little, 11U);
        CHECK_EQ(*big, 11U);
        CHECK_EQ(to_hex(output), "d855480000803f000020c0d851483f800000c0200000");

        const auto         typed_input = to_bytes("d855480000803f000020c0d851483f800000c0200000");
        auto               dec         = make_decoder_with_options<count_options, codec::typed_array>(typed_input);
        std::vector<float> little_values, big_values;
        const auto         little_decoded = dec(rfc8746::as_typed_array(little_values));
        const auto         big_decoded    = dec(rfc8746::as_typed_array_be(big_values));
        REQUIRE(little_decoded);
        REQUIRE(big_decoded);
        CHECK_EQ(*little_decoded, 11U);
        CHECK_EQ(*big_decoded, 11U);
        CHECK_EQ(little_values, values);
        CHECK_EQ(big_values, values);
    }

    TEST_CASE("count failures retain existing status and terminal prefixes") {
        std::array<std::byte, 2> storage{};
        auto                     output = std::span{storage};
        const auto               full   = make_encoder_with_options<count_options>(output)(1U, 24U);
        REQUIRE_FALSE(full);
        // Fixed-buffer exhaustion uses the existing runtime-exception boundary.
        CHECK_EQ(full.error(), status_code::error);
        CHECK_EQ(storage[0], std::byte{0x01});

        std::array<std::byte, 2> default_storage{};
        const auto               default_full = make_encoder(default_storage)(1U, 24U);
        REQUIRE_FALSE(default_full);
        CHECK_EQ(default_full.error(), status_code::error);
        CHECK_EQ(storage, default_storage);

        const auto input = to_bytes("0118");
        unsigned   first{}, second{99};
        auto       dec        = make_decoder_with_options<count_options>(input);
        const auto incomplete = dec(first, second);
        REQUIRE_FALSE(incomplete);
        CHECK_EQ(incomplete.error(), status_code::incomplete);
        CHECK_EQ(first, 1U);
        CHECK_EQ(second, 99U);
        CHECK(dec.tell() == input.cend());

        const auto wrong_input = to_bytes("016178");
        auto       wrong_dec   = make_decoder_with_options<count_options>(wrong_input);
        const auto wrong       = wrong_dec(first, second);
        REQUIRE_FALSE(wrong);
        CHECK_EQ(wrong.error(), status_code::no_match_for_uint_on_buffer);

        std::vector<std::byte> written;
        const auto             custom_failure = make_encoder_with_options<count_options>(written)(7U, failing_value{}, 8U);
        REQUIRE_FALSE(custom_failure);
        CHECK_EQ(custom_failure.error(), status_code::size_limit_exceeded);
        CHECK_EQ(to_hex(written), "0701");

        const auto    custom_input = to_bytes("070108");
        failing_value destination;
        unsigned      before{}, after{99};
        auto          custom_dec            = make_decoder_with_options<count_options>(custom_input);
        const auto    custom_decode_failure = custom_dec(before, destination, after);
        REQUIRE_FALSE(custom_decode_failure);
        CHECK_EQ(custom_decode_failure.error(), status_code::size_limit_exceeded);
        CHECK_EQ(before, 7U);
        CHECK_EQ(destination.decoded, 1U);
        CHECK_EQ(after, 99U);
        CHECK(custom_dec.tell() == custom_input.cbegin() + 2);
    }

    TEST_CASE("unsized non-contiguous byte counts add no input traversal") {
        const auto            bytes = to_bytes("011818d84e48010000000200000007");
        counted_unsized_input input{bytes};
        auto                  dec = make_decoder_with_options<count_options, codec::typed_array>(input);
        unsigned              one{}, twenty_four{};
        const auto            scalars = dec(one, twenty_four);
        REQUIRE(scalars);
        CHECK_EQ(*scalars, 3U);
        CHECK_EQ(input.increments, 3U);
        std::vector<std::int32_t> values;
        const auto                typed = dec(rfc8746::as_typed_array(values));
        REQUIRE(typed);
        CHECK_EQ(*typed, 11U);
        CHECK_EQ(values, (std::vector<std::int32_t>{1, 2}));
        CHECK_EQ(input.increments, 14U);
        unsigned   following{};
        const auto last = dec(following);
        REQUIRE(last);
        CHECK_EQ(*last, 1U);
        CHECK_EQ(following, 7U);
        CHECK_EQ(input.increments, bytes.size());

        const auto            truncated_bytes = to_bytes("d84e4801000000ff");
        counted_unsized_input truncated{truncated_bytes};
        const auto failure = make_decoder_with_options<count_options, codec::typed_array>(truncated)(rfc8746::as_typed_array(values));
        REQUIRE_FALSE(failure);
        CHECK_EQ(failure.error(), status_code::incomplete);
        CHECK_EQ(values, (std::vector<std::int32_t>{1}));
        CHECK_EQ(truncated.increments, truncated_bytes.size());
    }

    TEST_CASE("status-only results do not measure output size") {
        measured_output defaults;
        defaults.throw_on_size_call = 1;
        REQUIRE(make_encoder(defaults)(1U, 24U));
        CHECK_EQ(defaults.size_calls, 0U);
        CHECK_EQ(to_hex(defaults.bytes), "011818");

        measured_output custom;
        custom.throw_on_size_call = 1;
        REQUIRE(make_encoder_with_options<status_options>(custom)(1U, 24U));
        CHECK_EQ(custom.size_calls, 0U);
        CHECK_EQ(to_hex(custom.bytes), "011818");

        const auto input = to_bytes("011818");
        unsigned   one{}, twenty_four{};
        REQUIRE(make_decoder_with_options<status_options>(input)(one, twenty_four));
        CHECK_EQ(one, 1U);
        CHECK_EQ(twenty_four, 24U);

        unsized_output unmeasured;
        REQUIRE(make_encoder(unmeasured)(1U, 24U));
        CHECK_EQ(to_hex(unmeasured.bytes), "011818");
    }

    TEST_CASE("count measurement failures use the normal encoder error boundary") {
        for (const std::size_t throw_at : {1U, 2U}) {
            measured_output output;
            output.throw_on_size_call = throw_at;
            const auto result         = make_encoder_with_options<count_options>(output)(1U);
            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), status_code::error);
            CHECK_EQ(output.size_calls, throw_at);
            CHECK_EQ(to_hex(output.bytes), throw_at == 1U ? "" : "01");
        }
    }

    TEST_CASE("byte count conversion rejects narrowing instead of wrapping") {
        using narrow_result = expected<std::uint8_t, status_code>;
        const auto accepted = detail::make_byte_count_result<narrow_result>(255U);
        const auto rejected = detail::make_byte_count_result<narrow_result>(256U);
        REQUIRE(accepted);
        CHECK_EQ(*accepted, 255U);
        REQUIRE_FALSE(rejected);
        CHECK_EQ(rejected.error(), status_code::size_limit_exceeded);
    }

} // TEST_SUITE
