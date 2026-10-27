#include "test_util.h"

#include <array>
#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using namespace cbor::tags;

namespace {

struct resumable_record {
    int                            number{};
    std::string                    text;
    std::vector<int>               items;
    std::map<std::string, int>     mapping;
    std::optional<int>             extra;
    std::variant<int, std::string> choice;

    bool operator==(const resumable_record &) const = default;
};

std::vector<std::byte> encode_in_small_chunks(const resumable_record &value) {
    auto                     operation = make_resumable_encoder()(value);
    std::vector<std::byte>   encoded;
    std::array<std::byte, 3> buffer{};
    for (std::size_t calls = 0; !operation.done() && calls < 1024U; ++calls) {
        const auto progress = operation(buffer);
        for (const auto prefix : progress.produced()) {
            for (const auto byte : prefix)
                encoded.push_back(byte);
        }
    }
    REQUIRE(operation.done());
    REQUIRE(operation.result());
    CHECK_EQ(*operation.result(), encoded.size());
    return encoded;
}

} // namespace

TEST_SUITE("roundtrip/resumable") {

    TEST_CASE("typed values survive partial output and one-byte input") {
        const resumable_record source{42, "hello", {1, -2, 65536}, {{"a", 3}, {"b", -4}}, 9, std::string{"choice"}};
        const auto             encoded = encode_in_small_chunks(source);
        resumable_record       decoded{};
        auto                   operation = make_resumable_decoder()(decoded);
        for (std::size_t index = 0; index < encoded.size() && !operation.done(); ++index) {
            const auto progress = operation(std::span{encoded}.subspan(index, 1));
            CHECK(progress.bytes <= 1U);
        }
        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK_EQ(*operation.result(), encoded.size());
        CHECK(decoded == source);
    }

    TEST_CASE("sequence and array element generators keep distinct boundaries") {
        const std::vector<int> source{1, -2, 3};
        std::vector<std::byte> sequence;
        auto                   encoder = make_encoder(sequence);
        for (auto value : source)
            REQUIRE(encoder(value));

        auto       items = make_resumable_decoder().items<int>();
        const auto input = items(sequence, end_of_input);
        CHECK(input.state == resumable_state::yielded);
        std::vector<int> decoded;
        for (;;) {
            auto next = items.next();
            REQUIRE(next.await_ready());
            if (!next.await_resume())
                break;
            decoded.push_back(items.value());
        }
        REQUIRE(items.result());
        CHECK(decoded == source);

        std::vector<std::byte> array;
        REQUIRE(make_encoder(array)(source));
        auto elements = make_resumable_decoder().elements<int>();
        (void)elements(array);
        decoded.clear();
        for (;;) {
            auto next = elements.next();
            REQUIRE(next.await_ready());
            if (!next.await_resume())
                break;
            decoded.push_back(elements.value());
        }
        REQUIRE(elements.result());
        CHECK(decoded == source);
    }

    TEST_CASE("payload generators expose borrowed noncontiguous ranges") {
        const std::vector<std::byte> source{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
        std::vector<std::byte>       encoded;
        REQUIRE(make_encoder(encoded)(source));
        const std::list<std::byte> input{encoded.begin(), encoded.end()};
        auto                       payload = make_resumable_decoder().bstr();
        (void)payload(input);
        std::vector<std::byte> decoded;
        for (;;) {
            auto next = payload.next();
            REQUIRE(next.await_ready());
            if (!next.await_resume())
                break;
            CHECK_FALSE(payload.value().try_span().has_value());
            for (const auto byte : payload.value())
                decoded.push_back(byte);
        }
        REQUIRE(payload.result());
        CHECK(decoded == source);
    }

    TEST_CASE("output generators use caller buffers until their item is acknowledged") {
        const std::vector<int>   source{1, 2, 300, -10};
        auto                     operation = make_resumable_encoder().chunks(source);
        std::array<std::byte, 2> first{};
        std::list<std::byte>     second(32);
        (void)operation(first, second);
        std::vector<std::byte> encoded;
        for (;;) {
            auto next = operation.next();
            REQUIRE(next.await_ready());
            if (!next.await_resume())
                break;
            for (const auto byte : operation.value())
                encoded.push_back(byte);
        }
        REQUIRE(operation.result());
        CHECK_EQ(*operation.result(), encoded.size());
        std::vector<int> decoded;
        REQUIRE(make_decoder(encoded)(decoded));
        CHECK(decoded == source);
    }
}
