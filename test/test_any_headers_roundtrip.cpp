#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <map>
#include <string>
#include <variant>
#include <vector>

using namespace cbor::tags;

TEST_SUITE("roundtrip/any_headers") {

    TEST_CASE("definite container headers allow incremental value decoding") {
        const std::vector<int>           source_array{10, 20, 30};
        const std::map<int, std::string> source_map{{1, "one"}, {2, "two"}};
        std::vector<std::byte>           encoded;
        REQUIRE(make_encoder(encoded)(source_array, source_map));
        auto dec = make_decoder(encoded);

        as_array_any array_header{};
        REQUIRE(dec(array_header));
        REQUIRE_FALSE(array_header.indefinite);
        REQUIRE_EQ(array_header.size, source_array.size());
        std::vector<int> decoded_array;
        for (std::uint64_t index = 0; index < array_header.size; ++index) {
            int value{};
            REQUIRE(dec(value));
            decoded_array.push_back(value);
        }
        CHECK_EQ(decoded_array, source_array);

        as_map_any map_header{};
        REQUIRE(dec(map_header));
        REQUIRE_FALSE(map_header.indefinite);
        REQUIRE_EQ(map_header.size, source_map.size());
        std::map<int, std::string> decoded_map;
        for (std::uint64_t index = 0; index < map_header.size; ++index) {
            int         key{};
            std::string value;
            REQUIRE(dec(key, value));
            REQUIRE(decoded_map.emplace(key, value).second);
        }
        CHECK_EQ(decoded_map, source_map);
    }

    TEST_CASE("definite string headers allow explicit payload decoding") {
        const std::string            source_text{"hello"};
        const std::vector<std::byte> source_bytes{std::byte{1}, std::byte{2}, std::byte{3}};
        std::vector<std::byte>       encoded;
        REQUIRE(make_encoder(encoded)(source_text, source_bytes, 42));
        auto dec = make_decoder(encoded);

        std::variant<std::uint64_t, as_text_any, as_bstr_any> token;
        REQUIRE(dec(token));
        REQUIRE(std::holds_alternative<as_text_any>(token));
        const auto text_header = std::get<as_text_any>(token);
        CHECK_FALSE(text_header.indefinite);
        CHECK_EQ(dec.decode_text_payload(text_header.size), source_text);

        REQUIRE(dec(token));
        REQUIRE(std::holds_alternative<as_bstr_any>(token));
        const auto byte_header = std::get<as_bstr_any>(token);
        CHECK_FALSE(byte_header.indefinite);
        const auto bytes = dec.decode_bstring_payload(byte_header.size);
        CHECK_EQ(std::vector<std::byte>(bytes.begin(), bytes.end()), source_bytes);
        REQUIRE(dec(token));
        REQUIRE(std::holds_alternative<std::uint64_t>(token));
        CHECK_EQ(std::get<std::uint64_t>(token), 42);
    }

    TEST_CASE("indefinite arrays allow incremental values through a break alternative") {
        const std::vector<int> source{10, 20, 30};
        std::vector<std::byte> encoded;
        REQUIRE(make_encoder(encoded)(as_indefinite{source}, 42));
        auto         dec = make_decoder(encoded);
        as_array_any header{};
        REQUIRE(dec(header));
        CHECK(header.indefinite);

        std::vector<int>                    decoded;
        std::variant<int, indefinite_break> token;
        for (std::size_t index = 0; index < source.size(); ++index) {
            REQUIRE(dec(token));
            REQUIRE(std::holds_alternative<int>(token));
            decoded.push_back(std::get<int>(token));
        }
        CHECK_EQ(decoded, source);
        REQUIRE(dec(token));
        CHECK(std::holds_alternative<indefinite_break>(token));
        REQUIRE(dec(token));
        REQUIRE(std::holds_alternative<int>(token));
        CHECK_EQ(std::get<int>(token), 42);
    }

    TEST_CASE("indefinite maps allow incremental pairs and an explicit closing break") {
        const std::map<int, std::string> source{{1, "one"}, {2, "two"}};
        std::vector<std::byte>           encoded;
        REQUIRE(make_encoder(encoded)(as_indefinite{source}));
        auto       dec = make_decoder(encoded);
        as_map_any header{};
        REQUIRE(dec(header));
        CHECK(header.indefinite);

        std::map<int, std::string> decoded;
        for (std::size_t index = 0; index < source.size(); ++index) {
            int         key{};
            std::string value;
            REQUIRE(dec(key, value));
            REQUIRE(decoded.emplace(key, value).second);
        }
        CHECK_EQ(decoded, source);
        indefinite_break end;
        REQUIRE(dec(end));
    }

    TEST_CASE("indefinite string headers expose their chunks and closing breaks") {
        const std::string            source_text{"hello"};
        const std::vector<std::byte> source_bytes{std::byte{1}, std::byte{2}, std::byte{3}};
        std::vector<std::byte>       encoded;
        REQUIRE(make_encoder(encoded)(as_indefinite{source_text}, as_indefinite{source_bytes}));
        auto        dec = make_decoder(encoded);
        as_text_any text_header{};
        REQUIRE(dec(text_header));
        CHECK(text_header.indefinite);
        std::string                                 decoded_text;
        std::variant<std::string, indefinite_break> text_chunk;
        while (true) {
            REQUIRE(dec(text_chunk));
            if (std::holds_alternative<indefinite_break>(text_chunk)) {
                break;
            }
            decoded_text += std::get<std::string>(text_chunk);
        }
        CHECK_EQ(decoded_text, source_text);

        as_bstr_any byte_header{};
        REQUIRE(dec(byte_header));
        CHECK(byte_header.indefinite);
        std::vector<std::byte>                                 decoded_bytes;
        std::variant<std::vector<std::byte>, indefinite_break> byte_chunk;
        while (true) {
            REQUIRE(dec(byte_chunk));
            if (std::holds_alternative<indefinite_break>(byte_chunk)) {
                break;
            }
            const auto &chunk = std::get<std::vector<std::byte>>(byte_chunk);
            decoded_bytes.insert(decoded_bytes.end(), chunk.begin(), chunk.end());
        }
        CHECK_EQ(decoded_bytes, source_bytes);
    }

} // TEST_SUITE("roundtrip/any_headers")
