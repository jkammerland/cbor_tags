#include "character_traits.h"

#include <array>
#include <cstddef>
#include <doctest/doctest.h>
#include <limits>

TEST_SUITE("test_util/character_traits") {
    TEST_CASE_TEMPLATE("character conversions preserve every value and distinguish EOF", Char, std::byte, signed char, unsigned char) {
        using traits = test_util::character_traits<Char>;
        for (unsigned int value = 0; value <= std::numeric_limits<unsigned char>::max(); ++value) {
            const auto character = static_cast<Char>(value);
            CHECK(traits::eq(traits::to_char_type(traits::to_int_type(character)), character));
            CHECK_FALSE(traits::eq_int_type(traits::to_int_type(character), traits::eof()));
            CHECK_EQ(traits::not_eof(traits::to_int_type(character)), traits::to_int_type(character));
        }
        CHECK_FALSE(traits::eq_int_type(traits::not_eof(traits::eof()), traits::eof()));
    }

    TEST_CASE_TEMPLATE("character operations preserve overlap and string semantics", Char, std::byte, signed char, unsigned char) {
        using traits                = test_util::character_traits<Char>;
        constexpr auto            a = static_cast<Char>('a');
        constexpr auto            b = static_cast<Char>('b');
        constexpr auto            c = static_cast<Char>('c');
        constexpr auto            d = static_cast<Char>('d');
        const std::array<Char, 5> original{a, b, c, d, Char{}};
        auto                      data = original;

        CHECK_EQ(traits::length(data.data()), 4);
        CHECK(traits::find(data.data(), data.size(), c) == data.data() + 2);
        CHECK(traits::find(data.data(), 2, c) == nullptr);
        CHECK_EQ(traits::compare(data.data(), original.data(), original.size()), 0);
        CHECK_LT(traits::compare(data.data(), data.data() + 1, 2), 0);
        CHECK_GT(traits::compare(data.data() + 1, data.data(), 2), 0);

        CHECK(traits::move(data.data() + 1, data.data(), 4) == data.data() + 1);
        CHECK(data == std::array<Char, 5>{a, a, b, c, d});
        CHECK(traits::move(data.data(), data.data() + 1, 4) == data.data());
        CHECK(data == std::array<Char, 5>{a, b, c, d, d});
        CHECK(traits::move(data.data(), data.data(), 5) == data.data());
        CHECK(data == std::array<Char, 5>{a, b, c, d, d});

        CHECK(traits::copy(data.data(), original.data(), original.size()) == data.data());
        CHECK(data == original);
        CHECK(traits::assign(data.data(), 3, d) == data.data());
        CHECK(data == std::array<Char, 5>{d, d, d, d, Char{}});
        CHECK(traits::copy(nullptr, nullptr, 0) == nullptr);
        CHECK(traits::move(nullptr, nullptr, 0) == nullptr);
        CHECK(traits::assign(nullptr, 0, a) == nullptr);

        constexpr auto moved = [=] {
            std::array<Char, 3> values{a, b, c};
            traits::move(values.data() + 1, values.data(), 2);
            return values;
        }();
        static_assert(moved == std::array<Char, 3>{a, a, b});
    }
}
