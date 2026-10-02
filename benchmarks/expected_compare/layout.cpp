#include <array>
#include <cbor_tags/expected.h>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>

struct alignas(64) aligned_payload {
    std::array<std::byte, 128> bytes;
};

template <typename T> void describe(const char *name) {
    std::cout << name << ',' << sizeof(T) << ',' << alignof(T) << ',' << std::is_trivially_copy_constructible_v<T> << ','
              << std::is_trivially_move_constructible_v<T> << ',' << std::is_trivially_copy_assignable_v<T> << ','
              << std::is_trivially_move_assignable_v<T> << ',' << std::is_trivially_destructible_v<T> << '\n';
}

int main() {
    using cbor::tags::expected;
    std::cout << "type,sizeof,alignof,trivial_copy_ctor,trivial_move_ctor,trivial_copy_assign,trivial_move_assign,trivial_destructor\n";
    describe<expected<void, std::uint8_t>>("void/u8");
    describe<expected<std::uint64_t, std::uint8_t>>("u64/u8");
    describe<expected<std::uint64_t, std::uint64_t>>("u64/u64");
    describe<expected<std::string, std::uint8_t>>("string/u8");
    describe<expected<std::string, std::string>>("string/string");
    describe<expected<std::array<std::uint64_t, 8>, std::uint8_t>>("array64/u8");
    describe<expected<aligned_payload, std::uint8_t>>("aligned128/u8");
    describe<expected<std::unique_ptr<int>, std::uint8_t>>("unique_ptr/u8");
}
