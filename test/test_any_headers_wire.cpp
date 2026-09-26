#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <iterator>
#include <limits>
#include <ranges>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

using namespace cbor::tags;

namespace {

template <typename Header> constexpr std::uint8_t wire_major() {
    if constexpr (std::same_as<Header, as_bstr_any>) {
        return 0x40;
    } else if constexpr (std::same_as<Header, as_text_any>) {
        return 0x60;
    } else if constexpr (std::same_as<Header, as_array_any>) {
        return 0x80;
    } else {
        static_assert(std::same_as<Header, as_map_any>);
        return 0xA0;
    }
}

struct CountingUnsizedHeaderRange {
    using value_type = std::uint8_t;

    struct iterator {
        using value_type        = std::uint8_t;
        using difference_type   = std::ptrdiff_t;
        using iterator_concept  = std::bidirectional_iterator_tag;
        using iterator_category = std::bidirectional_iterator_tag;

        const value_type *current{};
        std::size_t      *increments{};

        const value_type &operator*() const { return *current; }
        iterator         &operator++() {
            ++current;
            ++*increments;
            return *this;
        }
        iterator operator++(int) {
            auto old = *this;
            ++*this;
            return old;
        }
        iterator &operator--() {
            --current;
            return *this;
        }
        iterator operator--(int) {
            auto old = *this;
            --*this;
            return old;
        }
        friend bool operator==(const iterator &, const iterator &) = default;
    };

    std::vector<std::uint8_t> bytes;
    mutable std::size_t       increments{};
    iterator                  begin() const { return {bytes.data(), &increments}; }
    iterator                  end() const { return {bytes.data() + bytes.size(), &increments}; }
};

static_assert(std::ranges::bidirectional_range<CountingUnsizedHeaderRange>);
static_assert(!std::ranges::sized_range<CountingUnsizedHeaderRange>);
static_assert(!std::ranges::contiguous_range<CountingUnsizedHeaderRange>);

struct SignedSizeDequeByteRange {
    using value_type = std::byte;
    using size_type  = int;
    std::deque<std::byte> bytes;
    auto                  begin() const noexcept { return bytes.begin(); }
    auto                  end() const noexcept { return bytes.end(); }
    auto                  size() const noexcept { return static_cast<size_type>(bytes.size()); }
};

static_assert(CborInputBuffer<SignedSizeDequeByteRange>);
static_assert(std::same_as<typename decltype(make_decoder(std::declval<SignedSizeDequeByteRange &>()))::size_type, int>);
static_assert(IsIndefiniteBreak<indefinite_break>);
static_assert(!IsSimple<indefinite_break>);
static_assert(get_major_3_bit_tag<indefinite_break>() == std::byte{0xE0});

} // namespace

TEST_SUITE("cbor_wire/any_headers") {

    TEST_CASE_TEMPLATE("headers leave definite content at the cursor for each argument width", Header, as_array_any, as_map_any,
                       as_text_any, as_bstr_any) {
        struct wire_case {
            std::vector<std::uint8_t> argument;
            std::uint64_t             size;
        };
        const std::array cases{
            wire_case{{0x00}, 0},
            wire_case{{0x17}, 23},
            wire_case{{0x18, 0x18}, 24},
            wire_case{{0x19, 0x01, 0x00}, 256},
            wire_case{{0x1A, 0x00, 0x01, 0x00, 0x00}, 65536},
            wire_case{{0x1B, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00}, 4294967296ULL},
            wire_case{{0x1B, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, std::numeric_limits<std::uint64_t>::max()},
        };

        for (const auto &test : cases) {
            CAPTURE(test.size);
            auto input = test.argument;
            input.front() |= wire_major<Header>();
            input.push_back(0x07);
            auto   dec = make_decoder(input);
            Header header{99, true};
            REQUIRE(dec(header));
            CHECK_EQ(header.size, test.size);
            CHECK_FALSE(header.indefinite);
            CHECK(dec.tell() == input.cbegin() + static_cast<std::ptrdiff_t>(test.argument.size()));
            std::uint64_t following{};
            REQUIRE(dec(following));
            CHECK_EQ(following, 7);
            CHECK(dec.tell() == input.cend());

            auto                                                  variant_dec = make_decoder(input);
            std::variant<std::uint64_t, Header, indefinite_break> token;
            REQUIRE(variant_dec(token));
            REQUIRE(std::holds_alternative<Header>(token));
            CHECK_EQ(std::get<Header>(token).size, test.size);
            CHECK_FALSE(std::get<Header>(token).indefinite);
            CHECK(variant_dec.tell() == input.cbegin() + static_cast<std::ptrdiff_t>(test.argument.size()));
        }
    }

    TEST_CASE_TEMPLATE("headers succeed without any declared content or closing break", Header, as_array_any, as_map_any, as_text_any,
                       as_bstr_any) {
        for (const auto ai : {0x03, 0x1F}) {
            const std::vector<std::uint8_t> input{static_cast<std::uint8_t>(wire_major<Header>() | ai)};
            auto                            dec = make_decoder(input);
            Header                          header{99, false};
            REQUIRE(dec(header));
            CHECK_EQ(header.size, ai == 0x1F ? 0 : 3);
            CHECK_EQ(header.indefinite, ai == 0x1F);
            CHECK(dec.tell() == input.cend());

            auto                                                  variant_dec = make_decoder(input);
            std::variant<std::uint64_t, Header, indefinite_break> token;
            REQUIRE(variant_dec(token));
            REQUIRE(std::holds_alternative<Header>(token));
            CHECK_EQ(std::get<Header>(token).size, ai == 0x1F ? 0 : 3);
            CHECK_EQ(std::get<Header>(token).indefinite, ai == 0x1F);
            CHECK(variant_dec.tell() == input.cend());
        }
    }

    TEST_CASE_TEMPLATE("indefinite headers consume one byte on supported input ranges", Header, as_array_any, as_map_any, as_text_any,
                       as_bstr_any) {
        const std::vector<std::uint8_t> bytes{static_cast<std::uint8_t>(wire_major<Header>() | 0x1F), 0x07, 0xFF};
        auto                            check = []<typename Input>(const Input &input) {
            auto   dec = make_decoder(input);
            Header header{99, false};
            REQUIRE(dec(header));
            CHECK_EQ(header.size, 0);
            CHECK(header.indefinite);
            if constexpr (std::same_as<Input, CountingUnsizedHeaderRange>) {
                CHECK_EQ(input.increments, 1);
                CHECK(dec.tell().current == input.bytes.data() + 1);
            } else {
                CHECK(dec.tell() == std::next(input.cbegin()));
            }
            std::uint64_t    value{};
            indefinite_break end;
            REQUIRE(dec(value, end));
            CHECK_EQ(value, 7);
            CHECK(dec.tell() == std::ranges::end(input));
            if constexpr (std::same_as<Input, CountingUnsizedHeaderRange>) {
                CHECK_EQ(input.increments, 3);
            }
            auto                                                  variant_dec = make_decoder(input);
            std::variant<std::uint64_t, Header, indefinite_break> token;
            REQUIRE(variant_dec(token));
            REQUIRE(std::holds_alternative<Header>(token));
            CHECK_EQ(std::get<Header>(token).size, 0);
            CHECK(std::get<Header>(token).indefinite);
            if constexpr (std::same_as<Input, CountingUnsizedHeaderRange>) {
                CHECK_EQ(input.increments, 4);
                CHECK(variant_dec.tell().current == input.bytes.data() + 1);
            } else {
                CHECK(variant_dec.tell() == std::next(input.cbegin()));
            }
        };
        check(bytes);
        check(std::deque<std::uint8_t>(bytes.begin(), bytes.end()));
        check(CountingUnsizedHeaderRange{bytes});
    }

    TEST_CASE_TEMPLATE("definite direct and variant headers consume only their arguments on supported ranges", Header, as_array_any,
                       as_map_any, as_text_any, as_bstr_any) {
        const std::vector<std::uint8_t> bytes{static_cast<std::uint8_t>(wire_major<Header>() | 0x19), 0x01, 0x00, 0x07};
        auto                            check = []<typename Input>(const Input &input) {
            auto   dec = make_decoder(input);
            Header header{99, true};
            REQUIRE(dec(header));
            CHECK_EQ(header.size, 256);
            CHECK_FALSE(header.indefinite);
            if constexpr (std::same_as<Input, CountingUnsizedHeaderRange>) {
                CHECK_EQ(input.increments, 3);
                CHECK(dec.tell().current == input.bytes.data() + 3);
            } else {
                CHECK(dec.tell() == std::next(input.cbegin(), 3));
            }
            auto                                                  variant_dec = make_decoder(input);
            std::variant<std::uint64_t, Header, indefinite_break> token;
            REQUIRE(variant_dec(token));
            REQUIRE(std::holds_alternative<Header>(token));
            CHECK_EQ(std::get<Header>(token).size, 256);
            CHECK_FALSE(std::get<Header>(token).indefinite);
            if constexpr (std::same_as<Input, CountingUnsizedHeaderRange>) {
                CHECK_EQ(input.increments, 6);
                CHECK(variant_dec.tell().current == input.bytes.data() + 3);
            } else {
                CHECK(variant_dec.tell() == std::next(input.cbegin(), 3));
            }
        };
        check(bytes);
        check(std::deque<std::uint8_t>(bytes.begin(), bytes.end()));
        check(CountingUnsizedHeaderRange{bytes});
    }

    TEST_CASE_TEMPLATE("reused direct and variant headers reset size and indefinite state", Header, as_array_any, as_map_any, as_text_any,
                       as_bstr_any) {
        const std::vector<std::uint8_t> input{static_cast<std::uint8_t>(wire_major<Header>() | 0x1F),
                                              static_cast<std::uint8_t>(wire_major<Header>() | 0x03),
                                              static_cast<std::uint8_t>(wire_major<Header>() | 0x1F), wire_major<Header>()};
        auto                            dec         = make_decoder(input);
        auto                            variant_dec = make_decoder(input);
        Header                          header{42, false};
        std::variant<std::uint64_t, Header, indefinite_break> token{Header{42, false}};
        for (const auto expected : {Header{0, true}, Header{3, false}, Header{0, true}, Header{0, false}}) {
            REQUIRE(dec(header));
            CHECK_EQ(header.size, expected.size);
            CHECK_EQ(header.indefinite, expected.indefinite);
            REQUIRE(variant_dec(token));
            REQUIRE(std::holds_alternative<Header>(token));
            CHECK_EQ(std::get<Header>(token).size, expected.size);
            CHECK_EQ(std::get<Header>(token).indefinite, expected.indefinite);
        }
        CHECK(dec.tell() == input.cend());
        CHECK(variant_dec.tell() == input.cend());
    }

    TEST_CASE_TEMPLATE("incomplete length arguments fail without reading past the input", Header, as_array_any, as_map_any, as_text_any,
                       as_bstr_any) {
        for (const auto [ai, width] : {std::pair{0x18, 1}, {0x19, 2}, {0x1A, 4}, {0x1B, 8}}) {
            for (int available = 0; available < width; ++available) {
                CAPTURE(ai);
                CAPTURE(available);
                std::vector<std::uint8_t> input(1 + available, 0);
                input.front() = static_cast<std::uint8_t>(wire_major<Header>() | ai);
                auto   dec    = make_decoder(input);
                Header header{};
                auto   result = dec(header);
                REQUIRE_FALSE(result);
                CHECK_EQ(result.error(), status_code::incomplete);

                auto                                                  variant_dec = make_decoder(input);
                std::variant<std::uint64_t, Header, indefinite_break> token;
                auto                                                  variant_result = variant_dec(token);
                REQUIRE_FALSE(variant_result);
                CHECK_EQ(variant_result.error(), status_code::incomplete);

                CountingUnsizedHeaderRange unsized{input};
                auto                       unsized_dec    = make_decoder(unsized);
                auto                       unsized_result = unsized_dec(header);
                REQUIRE_FALSE(unsized_result);
                CHECK_EQ(unsized_result.error(), status_code::incomplete);
                CHECK_EQ(unsized.increments, input.size());
                CHECK(unsized_dec.tell() == unsized.end());
            }
        }
    }

    TEST_CASE_TEMPLATE("reserved header additional information is rejected", Header, as_array_any, as_map_any, as_text_any, as_bstr_any) {
        for (const auto ai : {0x1C, 0x1D, 0x1E}) {
            const std::vector<std::uint8_t> input{static_cast<std::uint8_t>(wire_major<Header>() | ai)};
            Header                          header{};
            auto                            result = make_decoder(input)(header);
            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), status_code::error);
        }
    }

    TEST_CASE_TEMPLATE("bounded definite headers enforce size bounds without requiring content", Header, as_array_any, as_map_any,
                       as_text_any, as_bstr_any) {
        for (const auto size : {0, 1, 3, 4}) {
            const std::vector<std::uint8_t>                         input{static_cast<std::uint8_t>(wire_major<Header>() | size)};
            bounded_size<Header, 1, 3>                              fixed{Header{42, true}};
            dynamic_bounded_size<Header>                            dynamic{Header{42, true}, 1, 3};
            std::variant<std::uint64_t, bounded_size<Header, 1, 3>> token;
            auto                                                    fixed_dec      = make_decoder(input);
            auto                                                    dynamic_dec    = make_decoder(input);
            auto                                                    variant_dec    = make_decoder(input);
            const auto                                              fixed_result   = fixed_dec(fixed);
            const auto                                              dynamic_result = dynamic_dec(dynamic);
            const auto                                              variant_result = variant_dec(token);
            if (size == 1 || size == 3) {
                REQUIRE(fixed_result);
                CHECK_EQ(fixed.value().size, size);
                CHECK_FALSE(fixed.value().indefinite);
                REQUIRE(dynamic_result);
                CHECK_EQ(dynamic.value().size, size);
                CHECK_FALSE(dynamic.value().indefinite);
                REQUIRE(variant_result);
                REQUIRE_EQ(token.index(), 1);
                CHECK_EQ(std::get<1>(token).value().size, size);
                CHECK_FALSE(std::get<1>(token).value().indefinite);
                CHECK(fixed_dec.tell() == input.cend());
                CHECK(dynamic_dec.tell() == input.cend());
                CHECK(variant_dec.tell() == input.cend());
            } else {
                REQUIRE_FALSE(fixed_result);
                CHECK_EQ(fixed_result.error(), status_code::size_limit_exceeded);
                REQUIRE_FALSE(dynamic_result);
                CHECK_EQ(dynamic_result.error(), status_code::size_limit_exceeded);
                REQUIRE_FALSE(variant_result);
                CHECK_EQ(variant_result.error(), status_code::size_limit_exceeded);
            }
        }
    }

    TEST_CASE_TEMPLATE("bounded headers cannot establish nontrivial indefinite size bounds", Header, as_array_any, as_map_any, as_text_any,
                       as_bstr_any) {
        const std::vector<std::uint8_t>    input{static_cast<std::uint8_t>(wire_major<Header>() | 0x1F), 0xFF};
        constexpr auto                     unlimited = std::numeric_limits<std::size_t>::max();
        bounded_size<Header, 0, 4>         upper_bound;
        bounded_size<Header, 1, unlimited> lower_bound;
        auto                               upper_result = make_decoder(input)(upper_bound);
        REQUIRE_FALSE(upper_result);
        CHECK_EQ(upper_result.error(), status_code::size_limit_exceeded);
        auto lower_result = make_decoder(input)(lower_bound);
        REQUIRE_FALSE(lower_result);
        CHECK_EQ(lower_result.error(), status_code::size_limit_exceeded);

        for (const auto [min, max] : {std::pair<std::size_t, std::size_t>{0, 4}, {1, unlimited}}) {
            dynamic_bounded_size<Header> bounded{Header{}, min, max};
            auto                         result = make_decoder(input)(bounded);
            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        }
        std::variant<std::uint64_t, bounded_size<Header, 0, 4>> token;
        auto                                                    result = make_decoder(input)(token);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
    }

    TEST_CASE_TEMPLATE("unrestricted bounded headers preserve header-only semantics and reset state", Header, as_array_any, as_map_any,
                       as_text_any, as_bstr_any) {
        const std::vector<std::uint8_t>    input{static_cast<std::uint8_t>(wire_major<Header>() | 0x1F),
                                                 static_cast<std::uint8_t>(wire_major<Header>() | 0x03)};
        constexpr auto                     unlimited = std::numeric_limits<std::size_t>::max();
        bounded_size<Header, 0, unlimited> fixed{Header{42, false}};
        dynamic_bounded_size<Header>       dynamic{Header{42, false}, 0, unlimited};
        auto                               fixed_dec   = make_decoder(input);
        auto                               dynamic_dec = make_decoder(input);
        if constexpr (std::numeric_limits<std::size_t>::max() < std::numeric_limits<std::uint64_t>::max()) {
            const auto fixed_result = fixed_dec(fixed);
            REQUIRE_FALSE(fixed_result);
            CHECK_EQ(fixed_result.error(), status_code::size_limit_exceeded);
            const auto dynamic_result = dynamic_dec(dynamic);
            REQUIRE_FALSE(dynamic_result);
            CHECK_EQ(dynamic_result.error(), status_code::size_limit_exceeded);
            return;
        }
        for (const auto expected : {Header{0, true}, Header{3, false}}) {
            REQUIRE(fixed_dec(fixed));
            CHECK_EQ(fixed.value().size, expected.size);
            CHECK_EQ(fixed.value().indefinite, expected.indefinite);
            REQUIRE(dynamic_dec(dynamic));
            CHECK_EQ(dynamic.value().size, expected.size);
            CHECK_EQ(dynamic.value().indefinite, expected.indefinite);
        }
        CHECK(fixed_dec.tell() == input.cend());
        CHECK(dynamic_dec.tell() == input.cend());
    }

    TEST_CASE("break tokens work directly and in variants alongside other simple values") {
        const std::vector<std::uint8_t> input{0xFF, 0x07};
        auto                            dec = make_decoder(input);
        indefinite_break                end;
        REQUIRE(dec(end));
        CHECK(dec.tell() == input.cbegin() + 1);
        std::uint64_t next{};
        REQUIRE(dec(next));
        CHECK_EQ(next, 7);

        const std::vector<std::uint8_t>                              simple_input{0xF0, 0xF5, 0xF6, 0xFF};
        auto                                                         variant_dec = make_decoder(simple_input);
        std::variant<simple, bool, std::nullptr_t, indefinite_break> token;
        REQUIRE(variant_dec(token));
        REQUIRE(std::holds_alternative<simple>(token));
        CHECK_EQ(std::get<simple>(token).value, 16);
        REQUIRE(variant_dec(token));
        REQUIRE(std::holds_alternative<bool>(token));
        CHECK(std::get<bool>(token));
        REQUIRE(variant_dec(token));
        CHECK(std::holds_alternative<std::nullptr_t>(token));
        REQUIRE(variant_dec(token));
        CHECK(std::holds_alternative<indefinite_break>(token));
        CHECK(variant_dec.tell() == simple_input.cend());
    }

    TEST_CASE("break tokens reject non-break values and report absent input") {
        const std::vector<std::uint8_t> wrong{0xF6};
        indefinite_break                end;
        CHECK_FALSE(make_decoder(wrong)(end));
        const std::vector<std::uint8_t> empty;
        const auto                      result = make_decoder(empty)(end);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
    }

    // These regressions exercise explicit payload consumption. A header alone does
    // not promise that its declared payload is present in the admitted input.
    TEST_CASE("incomplete owning string variants propagate payload failure") {
        const std::vector<std::byte>             input{std::byte{0x62}, std::byte{'a'}};
        std::variant<std::uint64_t, std::string> decoded;
        const auto                               result = make_decoder(input)(decoded);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
    }

    TEST_CASE("unsized string header and explicit payload consumption each traverse once") {
        SUBCASE("byte string") {
            CountingUnsizedHeaderRange input{{0x43, 0x01, 0x02, 0x03}};
            auto                       dec = make_decoder(input);
            as_bstr_any                header{};
            REQUIRE(dec(header));
            CHECK_EQ(header.size, 3);
            CHECK_EQ(input.increments, 1);
            const auto payload = dec.decode_bstring_payload(header.size);
            CHECK_EQ(input.increments, input.bytes.size());
            CHECK(dec.tell() == input.end());
            CHECK_EQ(std::ranges::distance(payload), 3);
        }
        SUBCASE("text string") {
            CountingUnsizedHeaderRange input{{0x62, 'o', 'k'}};
            auto                       dec = make_decoder(input);
            as_text_any                header{};
            REQUIRE(dec(header));
            CHECK_EQ(header.size, 2);
            CHECK_EQ(input.increments, 1);
            const auto payload = dec.decode_text_payload(header.size);
            CHECK_EQ(input.increments, input.bytes.size());
            CHECK(dec.tell() == input.end());
            CHECK_EQ(std::ranges::distance(payload), 2);
        }
    }

    TEST_CASE("incomplete unsized payload consumption retains its destructive cursor") {
        SUBCASE("explicit byte payload") {
            CountingUnsizedHeaderRange input{{0x45, 0x01, 0x02}};
            auto                       dec = make_decoder(input);
            as_bstr_any                header{};
            REQUIRE(dec(header));
            CHECK_EQ(header.size, 5);
            CHECK_EQ(input.increments, 1);
            CHECK_THROWS_AS(dec.decode_bstring_payload(header.size), parse_incomplete_exception);
            CHECK_EQ(input.increments, input.bytes.size());
            CHECK(dec.tell() == input.end());
        }
        SUBCASE("owning text retains its consumed prefix") {
            CountingUnsizedHeaderRange input{{0x65, 'o', 'k'}};
            std::string                decoded{"prefix:"};
            auto                       dec    = make_decoder(input);
            const auto                 result = dec(decoded);
            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), status_code::incomplete);
            CHECK_EQ(decoded, "prefix:ok");
            CHECK_EQ(input.increments, input.bytes.size());
            CHECK(dec.tell() == input.end());
        }
    }

    TEST_CASE("explicit byte payload consumption supports signed-size non-contiguous ranges") {
        SignedSizeDequeByteRange input{{std::byte{0x41}, std::byte{0x01}, std::byte{0x02}}};
        auto                     dec = make_decoder(input);
        as_bstr_any              header{};
        REQUIRE(dec(header));
        CHECK_EQ(header.size, 1);
        CHECK(dec.tell() == std::next(input.begin()));
        const auto payload = dec.decode_bstring_payload(header.size);
        CHECK_EQ(*payload.begin(), std::byte{0x01});
        std::uint8_t following{};
        REQUIRE(dec(following));
        CHECK_EQ(following, 2);
        CHECK(dec.tell() == input.end());
    }

    TEST_CASE("explicit text payload consumption supports signed-size non-contiguous ranges") {
        SignedSizeDequeByteRange input{{std::byte{0x61}, std::byte{'A'}, std::byte{0x02}}};
        auto                     dec = make_decoder(input);
        as_text_any              header{};
        REQUIRE(dec(header));
        CHECK_EQ(header.size, 1);
        CHECK(dec.tell() == std::next(input.begin()));
        const auto payload = dec.decode_text_payload(header.size);
        CHECK_EQ(*payload.begin(), 'A');
        std::uint8_t following{};
        REQUIRE(dec(following));
        CHECK_EQ(following, 2);
        CHECK(dec.tell() == input.end());
    }

    TEST_CASE("explicit text payload consumption validates contiguous available bytes") {
        const std::vector<std::byte> input{std::byte{0x65}, std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
        auto                         dec = make_decoder(input);
        as_text_any                  header{};
        REQUIRE(dec(header));
        CHECK_EQ(header.size, 5);
        CHECK_THROWS_AS(dec.decode_text_payload(header.size), parse_incomplete_exception);
        CHECK(dec.tell() == input.cbegin() + 1);
    }

    TEST_CASE("explicit text payload consumption cannot advance sized iterators past end") {
        const std::deque<std::byte> input{std::byte{0x65}, std::byte{'a'}};
        auto                        dec = make_decoder(input);
        as_text_any                 header{};
        REQUIRE(dec(header));
        CHECK_EQ(header.size, 5);
        CHECK_THROWS_AS(dec.decode_text_payload(header.size), parse_incomplete_exception);
        CHECK(dec.tell() == std::next(input.cbegin()));
    }

    TEST_CASE("explicit successive byte payloads cannot advance sized iterators past end") {
        const std::deque<std::uint8_t> input{0x41, 0xAA, 0x41};
        auto                           dec = make_decoder(input);
        as_bstr_any                    first{}, second{};
        REQUIRE(dec(first));
        CHECK_EQ(first.size, 1);
        const auto payload = dec.decode_bstring_payload(first.size);
        CHECK_EQ(*payload.begin(), std::byte{0xAA});
        REQUIRE(dec(second));
        CHECK_EQ(second.size, 1);
        CHECK(dec.tell() == input.cend());
        CHECK_THROWS_AS(dec.decode_bstring_payload(second.size), parse_incomplete_exception);
        CHECK(dec.tell() == input.cend());
    }

    TEST_CASE("non-contiguous byte views update offsets for subsequent explicit payload bounds") {
        const std::deque<std::byte> input{std::byte{0x45}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
                                          std::byte{0x04}, std::byte{0x05}, std::byte{0x43}, std::byte{0xAA}};
        auto                        dec = make_decoder(input);
        decltype(dec)::bstr_view_t  first{};
        as_bstr_any                 second{};
        REQUIRE(dec(first, second));
        CHECK_EQ(std::ranges::distance(first), 5);
        CHECK_EQ(second.size, 3);
        CHECK(dec.tell() == std::next(input.cbegin(), 7));
        CHECK_THROWS_AS(dec.decode_bstring_payload(second.size), parse_incomplete_exception);
        CHECK(dec.tell() == std::next(input.cbegin(), 7));
    }

} // TEST_SUITE("cbor_wire/any_headers")
