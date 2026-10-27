#include <array>
#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

using namespace cbor::tags;

namespace {

// The synchronous form deliberately cannot instantiate against a lazy task.
// Its explicit replacement must win before ordinary method detection runs.
template <bool Synchronous, bool Transcode> struct constrained_record {
    int                            number{};
    inline static std::size_t      calls{};
    friend constexpr std::uint64_t cbor_tag(const constrained_record &) { return 100; }

    template <typename Context> auto decode(Context &context) {
        auto result = context(number);
        if constexpr (Synchronous) {
            if (!result)
                return result;
        }
        return result;
    }
};

template <typename Context, bool Synchronous, bool Transcode>
typename Context::task_type decode_constrained(Context &context, constrained_record<Synchronous, Transcode> &value) {
    ++value.calls;
    const auto status = co_await context(value.number);
    if (status != status_code::success)
        co_return status;
    co_return value.number > 100 ? status_code::size_limit_exceeded : status_code::success;
}

template <typename Context, bool Synchronous>
typename Context::task_type resumable_decode(Context &context, constrained_record<Synchronous, false> &value) {
    co_return co_await decode_constrained(context, value);
}

template <typename Context, bool Synchronous>
typename Context::task_type resumable_transcode(Context &context, constrained_record<Synchronous, true> &value) {
    co_return co_await decode_constrained(context, value);
}

template <typename Operation> void feed_bytes(Operation &operation, const std::vector<std::byte> &encoded) {
    for (const auto &byte : encoded) {
        if (operation.done())
            break;
        (void)operation(std::span{&byte, 1});
    }
    if (!operation.done())
        (void)operation(end_of_input);
    REQUIRE(operation.done());
}

struct encoded_record {
    static constexpr std::uint64_t cbor_tag = 100;
    int                            number{};
};

template <typename Record> void check_explicit_hook_priority(int source) {
    CAPTURE(source);
    std::vector<std::byte> encoded;
    REQUIRE(make_encoder(encoded)(static_tag<100>{}, source));
    const auto check = [&](auto &destination) {
        Record::calls  = 0;
        auto operation = make_resumable_decoder()(destination);
        feed_bytes(operation, encoded);
        CHECK_EQ(Record::calls, 1U);
        if (source > 100) {
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::size_limit_exceeded);
        } else
            REQUIRE(operation.result());
    };

    Record direct{};
    check(direct);
    std::optional<Record> optional;
    check(optional);
    std::variant<std::optional<Record>, int> variant;
    check(variant);
    if (source <= 100) {
        CHECK_EQ(direct.number, source);
        REQUIRE(optional);
        CHECK_EQ(optional->number, source);
        REQUIRE_EQ(variant.index(), 0U);
        REQUIRE(std::get<0>(variant));
        CHECK_EQ(std::get<0>(variant)->number, source);
    }

    // Both container routes must preserve the same hook and its failure status.
    for (bool indefinite : {false, true}) {
        CAPTURE(indefinite);
        const std::vector<encoded_record> values{{source}};
        std::vector<std::byte>            array;
        if (indefinite)
            REQUIRE(make_encoder(array)(as_indefinite{values}));
        else
            REQUIRE(make_encoder(array)(values));
        std::vector<Record> decoded;
        Record::calls  = 0;
        auto operation = make_resumable_decoder()(decoded);
        feed_bytes(operation, array);
        INFO((operation.result() ? "success" : status_message(operation.result().error())));
        CHECK_EQ(Record::calls, 1U);
        if (source > 100) {
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::size_limit_exceeded);
            CHECK(decoded.empty());
        } else {
            REQUIRE(operation.result());
            REQUIRE_EQ(decoded.size(), 1U);
            CHECK_EQ(decoded.front().number, source);
        }
    }

    // Generator shape checks must also avoid the replaced synchronous body.
    Record::calls  = 0;
    auto generator = make_resumable_decoder().items<Record>();
    (void)generator(encoded, end_of_input);
    auto next = generator.next();
    REQUIRE(next.await_ready());
    const auto yielded = next.await_resume();
    CHECK_EQ(Record::calls, 1U);
    if (source > 100) {
        CHECK_FALSE(yielded);
        REQUIRE_FALSE(generator.result());
        CHECK(generator.result().error() == status_code::size_limit_exceeded);
    } else {
        REQUIRE(yielded);
        CHECK_EQ(generator.value().number, source);
        auto last = generator.next();
        REQUIRE(last.await_ready());
        CHECK_FALSE(last.await_resume());
        REQUIRE(generator.result());
    }
}

struct codec_record {
    static constexpr std::uint64_t cbor_tag = 100;
    int                            number{};
    codec_record() {}
};
struct later_record {
    static constexpr std::uint64_t cbor_tag = 101;
    int                            number{};
};

template <typename Context> struct tag_codec : codec::decoder_base<Context> {
    using codec::decoder_base<Context>::decode;

    status_code decode(codec_record &value, std::uint64_t tag) {
        if (tag != codec_record::cbor_tag)
            return status_code::no_match_for_tag;
        return static_cast<Context &>(*this).decode(value.number);
    }
    status_code decode(codec_record &value, major_type major, std::byte info) {
        if (major != major_type::Tag)
            return status_code::no_match_for_tag_on_buffer;
        return decode(value, static_cast<Context &>(*this).decode_unsigned(info));
    }
    resumable_task<status_code> resumable_decode(codec_record &value, std::uint64_t tag) {
        if (tag != codec_record::cbor_tag)
            co_return status_code::no_match_for_tag;
        co_return co_await static_cast<Context &>(*this)(value.number);
    }
    resumable_task<status_code> resumable_decode(codec_record &value, major_type major, std::byte info) {
        if (major != major_type::Tag)
            co_return status_code::no_match_for_tag_on_buffer;
        auto &context = static_cast<Context &>(*this);
        co_return co_await resumable_decode(value, co_await context.argument(info));
    }
};

template <typename Context> struct task_tag_codec {
    resumable_task<status_code> decode(codec_record &value, std::uint64_t tag) {
        if (tag != codec_record::cbor_tag)
            co_return status_code::no_match_for_tag;
        co_return co_await static_cast<Context &>(*this)(value.number);
    }
};

template <template <typename> typename Codec> void check_tag_codec() {
    std::vector<std::byte> encoded;
    REQUIRE(make_encoder(encoded)(static_tag<100>{}, 300));
    codec_record direct;
    auto         direct_op = make_resumable_decoder<Codec>()(direct);
    feed_bytes(direct_op, encoded);
    REQUIRE(direct_op.result());
    CHECK_EQ(direct.number, 300);

    std::variant<int, std::optional<codec_record>> nested;
    auto                                           nested_op = make_resumable_decoder<Codec>()(nested);
    feed_bytes(nested_op, encoded);
    REQUIRE(nested_op.result());
    REQUIRE_EQ(nested.index(), 1U);
    REQUIRE(std::get<1>(nested));
    CHECK_EQ(std::get<1>(nested)->number, 300);

    std::vector<std::byte> later;
    REQUIRE(make_encoder(later)(later_record{41}));
    std::variant<codec_record, later_record> destination;
    auto                                     later_op = make_resumable_decoder<Codec>()(destination);
    feed_bytes(later_op, later);
    REQUIRE(later_op.result());
    REQUIRE_EQ(destination.index(), 1U);
    CHECK_EQ(std::get<1>(destination).number, 41);
}

template <typename Value, typename Expected> void check_const_encoding(const Value &value, const Expected &expected) {
    auto                   operation = make_resumable_encoder()(value);
    std::vector<std::byte> encoded;
    // Exercise suspension within both headers and payloads.
    for (std::size_t calls = 0; !operation.done() && calls < 256; ++calls) {
        std::array<std::byte, 1> buffer{};
        const auto               progress = operation(buffer);
        for (const auto prefix : progress.produced())
            for (const auto byte : prefix)
                encoded.push_back(byte);
    }
    REQUIRE(operation.done());
    REQUIRE(operation.result());
    CHECK_EQ(*operation.result(), encoded.size());
    Expected decoded{};
    REQUIRE(make_decoder(encoded)(decoded));
    CHECK(decoded == expected);
}

template <typename Wrapper, typename Expected> void check_const_and_nested(const Wrapper &wrapper, const Expected &expected) {
    check_const_encoding(wrapper, expected);
    const std::optional<Wrapper> optional{wrapper};
    check_const_encoding(optional, expected);
    const std::tuple<Wrapper> tuple{wrapper};
    check_const_encoding(tuple, expected);
}

struct equality_end {
    const std::byte *last{};
    friend bool      operator==(const std::byte *current, equality_end end) { return current == end.last; }
};

} // namespace

TEST_SUITE("roundtrip/resumable_review") {
    TEST_CASE("explicit hooks retain priority through nested decode routes") {
        for (int source : {42, 300}) {
            check_explicit_hook_priority<constrained_record<false, false>>(source);
            check_explicit_hook_priority<constrained_record<true, false>>(source);
            check_explicit_hook_priority<constrained_record<false, true>>(source);
            check_explicit_hook_priority<constrained_record<true, true>>(source);
        }
    }

    TEST_CASE("cached tag dispatch selects named and task-returning codec mixins") {
        check_tag_codec<tag_codec>();
        check_tag_codec<task_tag_codec>();
    }

    TEST_CASE("const explicit range wrappers encode directly and when nested") {
        std::vector<int>       values{1, -2, 300};
        std::map<int, int>     mapping{{1, 2}, {3, 4}};
        std::string            text = "const text";
        std::vector<std::byte> bytes{std::byte{1}, std::byte{2}, std::byte{3}};
        const auto             array = as_array_range(values);
        const auto             map   = as_map_range(mapping);
        const auto             tstr  = as_tstr_range(text);
        const auto             bstr  = as_bstr_range(bytes);
        check_const_and_nested(array, values);
        check_const_and_nested(map, mapping);
        check_const_and_nested(tstr, text);
        check_const_and_nested(bstr, bytes);
    }

    TEST_CASE("const bounded ranges check the underlying range for every wrapper kind") {
        std::vector<int>       values{1, 2};
        std::map<int, int>     mapping{{1, 2}, {3, 4}};
        std::string            text = "abc";
        std::vector<std::byte> bytes{std::byte{1}, std::byte{2}};
        const auto             static_array = as_bounded_size<1, 3>(as_array_range(values));
        const auto             static_map   = as_bounded_size<1, 3>(as_map_range(mapping));
        const auto             static_tstr  = as_bounded_size<1, 3>(as_tstr_range(text));
        const auto             static_bstr  = as_bounded_size<1, 3>(as_bstr_range(bytes));
        check_const_and_nested(static_array, values);
        check_const_and_nested(static_map, mapping);
        check_const_and_nested(static_tstr, text);
        check_const_and_nested(static_bstr, bytes);
        const auto dynamic_array = as_bounded_size(as_array_range(values), 1, 3);
        const auto dynamic_map   = as_bounded_size(as_map_range(mapping), 1, 3);
        const auto dynamic_tstr  = as_bounded_size(as_tstr_range(text), 1, 3);
        const auto dynamic_bstr  = as_bounded_size(as_bstr_range(bytes), 1, 3);
        check_const_and_nested(dynamic_array, values);
        check_const_and_nested(dynamic_map, mapping);
        check_const_and_nested(dynamic_tstr, text);
        check_const_and_nested(dynamic_bstr, bytes);

        const auto               too_small = as_bounded_size<0, 1>(as_array_range(values));
        auto                     operation = make_resumable_encoder()(too_small);
        std::array<std::byte, 8> buffer{};
        const auto               progress = operation(buffer);
        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::size_limit_exceeded);
        CHECK_EQ(progress.bytes, 0U);
    }

    TEST_CASE("borrowed text accepts contiguous chunks with non-common sentinels") {
        const std::string      source(40, 'x');
        std::vector<std::byte> encoded;
        REQUIRE(make_encoder(encoded)(source));
        const auto text_size = encoded.size();
        REQUIRE(make_encoder(encoded)(17));
        const auto check = [&](auto &range) {
            std::string_view decoded;
            auto             operation = make_resumable_decoder()(decoded);
            const auto       progress  = operation(range, end_of_input);
            REQUIRE(operation.done());
            REQUIRE(operation.result());
            CHECK_EQ(progress.bytes, text_size);
            CHECK(decoded == source);
            CHECK(decoded.data() == reinterpret_cast<const char *>(encoded.data() + text_size - source.size()));
        };
        auto sized = std::ranges::subrange(encoded.data(), equality_end{encoded.data() + encoded.size()}, encoded.size());
        static_assert(std::ranges::contiguous_range<decltype(sized)>);
        static_assert(std::ranges::sized_range<decltype(sized)>);
        static_assert(!std::sized_sentinel_for<equality_end, std::byte *>);
        check(sized);
        auto unsized = std::ranges::subrange(encoded.data(), equality_end{encoded.data() + encoded.size()});
        static_assert(!std::ranges::sized_range<decltype(unsized)>);
        check(unsized);

        bool inspected_tail{};
        auto view = std::span{encoded} | std::views::take_while([&](const std::byte &byte) {
                        if (&byte >= encoded.data() + text_size)
                            inspected_tail = true;
                        return true;
                    });
        static_assert(std::ranges::contiguous_range<decltype(view)>);
        check(view);
        CHECK_FALSE(inspected_tail);

        // A header can span feeds while the payload remains in one chunk.
        std::string_view split;
        auto             operation = make_resumable_decoder()(split);
        (void)operation(std::span{encoded}.first(1));
        auto rest = std::span{encoded}.subspan(1) | std::views::take_while([](std::byte) { return true; });
        (void)operation(rest, end_of_input);
        REQUIRE(operation.result());
        CHECK(split == source);
    }

    TEST_CASE("borrowed partial input keeps eof and split-storage failures distinct") {
        const std::string      source = "hello";
        std::vector<std::byte> encoded;
        REQUIRE(make_encoder(encoded)(source));
        auto             prefix = std::span{encoded}.first(encoded.size() - 1) | std::views::take_while([](std::byte) { return true; });
        std::string_view decoded;
        auto             operation = make_resumable_decoder()(decoded);
        CHECK(operation(prefix).state == resumable_state::need_input);
        CHECK(decoded.empty());
        const std::array<std::byte, 0> empty{};
        CHECK(operation(empty).state == resumable_state::need_input);
        (void)operation(end_of_input);
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::incomplete);

        auto split = make_resumable_decoder()(decoded);
        (void)split(prefix);
        (void)split(std::span{encoded}.last(1), end_of_input);
        REQUIRE_FALSE(split.result());
        CHECK(split.result().error() == status_code::contiguous_view_on_non_contiguous_data);
        CHECK(decoded.empty());
    }

    TEST_CASE("sized non-common chunk descriptors preserve their remaining extent") {
        const std::array<std::byte, 4> data{};
        auto                           range = std::ranges::subrange(data.data(), equality_end{data.data() + data.size()}, data.size());
        readable_chunk                 chunk{range};
        REQUIRE(chunk.try_span());
        CHECK_EQ(chunk.try_span()->size(), data.size());
        (void)chunk.read();
        readable_chunk copied{chunk};
        CHECK_EQ(chunk.try_span()->size(), data.size() - 1);
        std::size_t count{};
        auto        prefix = chunk.take(2, count);
        CHECK_EQ(count, 2U);
        REQUIRE(prefix.try_span());
        CHECK_EQ(prefix.try_span()->size(), count);
        CHECK_EQ(chunk.try_span()->size(), 1U);
        CHECK_EQ(copied.try_span()->size(), data.size() - 1);
    }

    TEST_CASE("direct volatile arrays can be supplied as readable and writable chunks") {
        const int              source = 42;
        std::vector<std::byte> encoded;
        REQUIRE(make_encoder(encoded)(source));
        volatile unsigned char storage[32]{};
        REQUIRE(encoded.size() <= std::size(storage));
        for (std::size_t index = 0; index < encoded.size(); ++index)
            storage[index] = std::to_integer<unsigned char>(encoded[index]);
        const volatile auto &input     = storage;
        auto                 validator = make_resumable_validator();
        (void)validator(input, end_of_input);
        REQUIRE(validator.result());
        CHECK_EQ(*validator.result(), encoded.size());
        int  decoded{};
        auto decoder = make_resumable_decoder()(decoded);
        (void)decoder(storage, end_of_input);
        REQUIRE(decoder.result());
        CHECK_EQ(decoded, source);

        volatile unsigned char output[32]{};
        auto                   encoder  = make_resumable_encoder()(source);
        const auto             progress = encoder(output);
        REQUIRE(encoder.result());
        std::vector<std::byte> produced;
        for (const auto prefix : progress.produced())
            for (const auto byte : prefix)
                produced.push_back(byte);
        int result{};
        REQUIRE(make_decoder(produced)(result));
        CHECK_EQ(result, source);
    }
}
