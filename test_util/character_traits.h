#pragma once

#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <ios>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

namespace test_util {

// Standard libraries need not provide char_traits for byte or signed/unsigned char.
template <typename Char> struct character_traits {
    static_assert(std::is_same_v<Char, std::byte> || std::is_same_v<Char, signed char> || std::is_same_v<Char, unsigned char>);

    using char_type  = Char;
    using int_type   = std::uint_least32_t;
    using off_type   = std::streamoff;
    using pos_type   = std::streampos;
    using state_type = std::mbstate_t;
    static_assert(std::numeric_limits<int_type>::max() > std::numeric_limits<unsigned char>::max());

    static constexpr void assign(char_type &destination, const char_type &source) noexcept { destination = source; }
    static constexpr bool eq(char_type lhs, char_type rhs) noexcept { return lhs == rhs; }
    static constexpr bool lt(char_type lhs, char_type rhs) noexcept { return to_int_type(lhs) < to_int_type(rhs); }

    static constexpr int compare(const char_type *lhs, const char_type *rhs, std::size_t count) noexcept {
        for (std::size_t i = 0; i < count; ++i) {
            if (lt(lhs[i], rhs[i])) {
                return -1;
            }
            if (lt(rhs[i], lhs[i])) {
                return 1;
            }
        }
        return 0;
    }

    static constexpr std::size_t length(const char_type *text) noexcept {
        std::size_t count = 0;
        while (!eq(text[count], char_type{})) {
            ++count;
        }
        return count;
    }

    static constexpr const char_type *find(const char_type *text, std::size_t count, const char_type &value) noexcept {
        for (std::size_t i = 0; i < count; ++i) {
            if (eq(text[i], value)) {
                return text + i;
            }
        }
        return nullptr;
    }

    static constexpr char_type *copy(char_type *destination, const char_type *source, std::size_t count) noexcept {
        for (std::size_t i = 0; i < count; ++i) {
            assign(destination[i], source[i]);
        }
        return destination;
    }

    static constexpr char_type *move(char_type *destination, const char_type *source, std::size_t count) noexcept {
        // Equality detects overlap without ordering pointers into unrelated arrays.
        for (std::size_t i = 0; i < count; ++i) {
            if (destination == source + i) {
                for (auto remaining = count; remaining != 0; --remaining) {
                    assign(destination[remaining - 1], source[remaining - 1]);
                }
                return destination;
            }
        }
        return copy(destination, source, count);
    }

    static constexpr char_type *assign(char_type *destination, std::size_t count, char_type value) noexcept {
        for (std::size_t i = 0; i < count; ++i) {
            assign(destination[i], value);
        }
        return destination;
    }

    static constexpr int_type  to_int_type(char_type value) noexcept { return static_cast<unsigned char>(value); }
    static constexpr char_type to_char_type(int_type value) noexcept { return static_cast<char_type>(value); }
    static constexpr bool      eq_int_type(int_type lhs, int_type rhs) noexcept { return lhs == rhs; }
    static constexpr int_type  eof() noexcept { return std::numeric_limits<int_type>::max(); }
    static constexpr int_type  not_eof(int_type value) noexcept { return eq_int_type(value, eof()) ? 0 : value; }
};

template <typename Char> using basic_string      = std::basic_string<Char, character_traits<Char>>;
template <typename Char> using basic_string_view = std::basic_string_view<Char, character_traits<Char>>;

} // namespace test_util
