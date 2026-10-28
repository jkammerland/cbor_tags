#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cbor_tags/codec.h>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <iterator>
#include <limits>
#include <list>
#include <ranges>
#include <span>
#include <type_traits>
#include <vector>

using namespace cbor::tags;

namespace {

using input_chunks = std::list<std::span<const std::byte>>;

struct override_calls {
    static inline std::size_t decoded{};
};

template <typename Self> struct direct_integer_codec : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;

    status_code decode(std::uint32_t &value) {
        std::uint64_t wire_value{};
        const auto    result = static_cast<Self &>(*this)(wire_value);
        if (!result)
            return result.error();
        if (wire_value < 1000 || wire_value - 1000 > std::numeric_limits<std::uint32_t>::max())
            return status_code::no_match_for_uint_on_buffer;
        value = static_cast<std::uint32_t>(wire_value - 1000);
        ++override_calls::decoded;
        return status_code::success;
    }
};

template <typename Self> struct header_integer_codec : codec::base<Self> {
    using codec::base<Self>::decode;
    using codec::base<Self>::encode;

    status_code decode(std::uint32_t &value, major_type major, std::byte info) {
        std::uint64_t wire_value{};
        const auto    status = static_cast<Self &>(*this).decode(wire_value, major, info);
        if (status != status_code::success)
            return status;
        if (wire_value < 2000 || wire_value - 2000 > std::numeric_limits<std::uint32_t>::max())
            return status_code::no_match_for_uint_on_buffer;
        value = static_cast<std::uint32_t>(wire_value - 2000);
        ++override_calls::decoded;
        return status_code::success;
    }
};

template <template <typename> typename Codec> void check_indefinite_integer_override(std::uint64_t shift) {
    const std::vector<std::uint32_t> expected{0, 24, 65536};
    const std::vector<std::uint64_t> shifted{shift, shift + 24, shift + 65536};
    std::vector<std::byte>           bytes;
    REQUIRE(make_encoder(bytes)(as_indefinite{shifted}));

    for (const bool bytewise : {false, true}) {
        CAPTURE(bytewise);
        input_chunks               storage;
        auto                       source = make_input_source(storage);
        std::vector<std::uint32_t> actual;
        override_calls::decoded = 0;
        auto operation          = make_resumable_decoder<Codec>(source)(actual);
        if (bytewise) {
            for (const auto &byte : bytes)
                operation(std::span{&byte, 1});
        } else {
            operation(bytes);
        }

        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(*operation.result() == bytes.size());
        CHECK(actual == expected);
        CHECK(override_calls::decoded == expected.size());
    }
}

} // namespace

TEST_SUITE("roundtrip/resumable") {

    TEST_CASE_TEMPLATE("indefinite integer deques retain native limits across every input split", T, std::uint32_t, std::uint64_t,
                       std::int32_t, std::int64_t) {
        std::deque<T> expected{0, 24, 256, 65536, std::numeric_limits<T>::max()};
        if constexpr (std::is_signed_v<T>) {
            expected.push_back(-25);
            expected.push_back(std::numeric_limits<T>::min());
        }
        std::vector<std::byte> bytes;
        REQUIRE(make_encoder(bytes)(as_indefinite{expected}));
        const std::span<const std::byte> input{bytes};

        for (std::size_t cut = 0; cut <= input.size(); ++cut) {
            CAPTURE(cut);
            input_chunks  storage;
            auto          source = make_input_source(storage);
            std::deque<T> actual;
            auto          indefinite = as_indefinite{actual};
            auto          operation  = make_resumable_decoder(source)(indefinite);
            operation(input.first(cut));
            if (!operation.done())
                operation(input.subspan(cut), end_of_input);
            REQUIRE(operation.done());
            REQUIRE(operation.result());
            CHECK(*operation.result() == bytes.size());
            CHECK(actual == expected);
        }
    }

    TEST_CASE("indefinite integers preserve direct and header codec dispatch") {
        check_indefinite_integer_override<direct_integer_codec>(1000);
        check_indefinite_integer_override<header_integer_codec>(2000);
    }

    TEST_CASE("indefinite integers cross admitted unsized chunks without an extra resume") {
        const std::vector<std::int64_t> expected{1, -25, 65536, std::numeric_limits<std::int64_t>::min()};
        std::vector<std::byte>          wire;
        REQUIRE(make_encoder(wire)(as_indefinite{expected}));
        std::list<std::byte> bytes(wire.begin(), wire.end());
        const auto           middle = std::next(bytes.begin(), wire.size() / 2);
        using chunk                 = std::ranges::subrange<std::list<std::byte>::iterator>;
        static_assert(!std::ranges::sized_range<chunk>);
        std::list<chunk>          storage;
        auto                      source = make_input_source(storage);
        std::vector<std::int64_t> actual{99};
        auto                      operation = make_resumable_decoder(source)(actual);
        operation(chunk{bytes.begin(), middle}, chunk{middle, bytes.end()});

        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(*operation.result() == wire.size());
        std::vector<std::int64_t> appended{99};
        appended.insert(appended.end(), expected.begin(), expected.end());
        CHECK(actual == appended);
    }
}
