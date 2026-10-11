#include "cwt_test_support.h"

TEST_SUITE("cbor_wire/cwt") {
    TEST_CASE("protected header decoding preserves destinations on malformed or trailing input") {
        const std::vector<std::byte>              previous_id{std::byte{7}};
        const header_map                          initial{.alg = algorithm::es384, .kid = previous_id};
        const std::vector<std::vector<std::byte>> malformed{
            {std::byte{0xa1}, std::byte{0x04}, std::byte{0x42}, std::byte{1}},
            {std::byte{0xa0}, std::byte{0x00}},
            {std::byte{0xa2}, std::byte{0x01}, std::byte{0x26}, std::byte{0x01}, std::byte{0x26}}};
        for (const auto &bytes : malformed) {
            header_map      owned    = initial;
            header_map_view borrowed = as_view(initial);
            CHECK_FALSE(decode_protected_header(bytes, owned));
            CHECK_FALSE(decode_protected_header(bytes, borrowed));
            CHECK(owned.alg == initial.alg);
            CHECK(owned.kid == initial.kid);
            CHECK(borrowed.alg == initial.alg);
            REQUIRE(borrowed.kid);
            CHECK(borrowed.kid->data() == initial.kid->data());
        }
    }
}
