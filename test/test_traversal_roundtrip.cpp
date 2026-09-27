#include <array>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_traversal.h>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <limits>
#include <map>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace cbor::tags;

namespace {

template <typename Self> struct traversal_tag_observer : cbor_decoder_mixin_base<Self> {
    using cbor_decoder_mixin_base<Self>::decode;

    std::vector<std::uint64_t> observed_tags;
    status_code                tag_status{status_code::success};

    status_code observe_decoded_cbor_tag(std::uint64_t tag) {
        observed_tags.push_back(tag);
        return tag_status;
    }
};

struct semantic_frame {
    std::size_t   depth;
    std::string   kind;
    std::uint64_t argument;
    bool          indefinite;
    friend bool   operator==(const semantic_frame &, const semantic_frame &) = default;
};

template <typename T> semantic_frame make_frame(const T &value, std::size_t depth) {
    if constexpr (std::same_as<T, as_array_any>) {
        return {depth, "array", value.size, value.indefinite};
    } else if constexpr (std::same_as<T, as_map_any>) {
        return {depth, "map", value.size, value.indefinite};
    } else if constexpr (std::same_as<T, as_text_any>) {
        return {depth, "text", value.size, value.indefinite};
    } else if constexpr (std::same_as<T, as_bstr_any>) {
        return {depth, "bytes", value.size, value.indefinite};
    } else {
        static_assert(std::same_as<T, as_tag_any>);
        return {depth, "tag", value.tag, false};
    }
}

} // namespace

TEST_SUITE("roundtrip/traversal") {

    TEST_CASE("generated scalar and borrowed text values share value events within balanced headers") {
        const std::map<std::uint64_t, std::vector<std::string>> source{{1, {"one", "", "three"}}, {2, {"four", "five"}}};
        const std::vector<std::uint64_t>                        expected_keys{1, 2};
        const std::vector<std::string>                          expected_text{"one", "", "three", "four", "five"};
        for (const bool indefinite : {false, true}) {
            CAPTURE(indefinite);
            std::vector<std::byte> encoded;
            auto                   enc = make_encoder(encoded);
            if (indefinite) {
                REQUIRE(enc(as_indefinite{source}));
            } else {
                REQUIRE(enc(source));
            }
            auto dec        = make_decoder(encoded);
            using text_type = decltype(dec.decode_text_payload(0));
            std::vector<semantic_frame> frames;
            std::vector<std::uint64_t>  keys;
            std::vector<std::string>    text;
            bool                        root_began = false;
            bool                        root_ended = false;
            REQUIRE(walk_item(dec, [&](const auto &value, const auto &context) {
                using T = std::remove_cvref_t<decltype(value)>;
                if constexpr (IsAnyHeader<T>) {
                    const auto frame = make_frame(value, context.depth);
                    if (context.kind == walk_event_kind::enter) {
                        CHECK_EQ(context.depth, frames.size());
                        if (frames.empty()) {
                            CHECK_FALSE(root_began);
                            root_began = true;
                            CHECK_EQ(frame.kind, "map");
                            CHECK_EQ(frame.indefinite, indefinite);
                        }
                        frames.push_back(frame);
                    } else {
                        REQUIRE(context.kind == walk_event_kind::leave);
                        REQUIRE_FALSE(frames.empty());
                        CHECK(frames.back() == frame);
                        frames.pop_back();
                        if (frames.empty()) {
                            root_ended = true;
                        }
                    }
                } else if constexpr (std::same_as<T, positive>) {
                    CHECK(context.kind == walk_event_kind::value);
                    CHECK_EQ(context.depth, frames.size());
                    keys.push_back(value);
                } else if constexpr (std::same_as<T, text_type>) {
                    REQUIRE(context.kind == walk_event_kind::value);
                    REQUIRE_FALSE(frames.empty());
                    CHECK_EQ(context.depth, frames.back().depth);
                    if (!value.empty()) {
                        REQUIRE_FALSE(context.source.empty());
                        CHECK(reinterpret_cast<const std::byte *>(value.data()) == &*context.source.begin());
                    }
                    text.emplace_back(value.begin(), value.end());
                } else {
                    FAIL("unexpected event for the generated map of arrays of text");
                }
            }));
            CHECK(root_began);
            CHECK(root_ended);
            CHECK(frames.empty());
            CHECK(keys == expected_keys);
            CHECK(text == expected_text);

            auto validation_dec = make_decoder(encoded);
            REQUIRE(validate_item(validation_dec));
            auto                                  typed_dec = make_decoder(encoded);
            std::remove_cvref_t<decltype(source)> decoded;
            REQUIRE(typed_dec(decoded));
            CHECK(decoded == source);
        }
    }

    TEST_CASE("generated scalar values reach visitors without narrowing or changing their type") {
        const auto check = []<typename T>(const T &source) {
            std::vector<std::byte> encoded;
            REQUIRE(make_encoder(encoded)(source));
            auto dec     = make_decoder(encoded);
            bool visited = false;
            REQUIRE(walk_item(dec,
                              [&](const auto &value, const auto &context) {
                                  using Value = std::remove_cvref_t<decltype(value)>;
                                  CHECK(context.kind == walk_event_kind::value);
                                  CHECK_EQ(context.depth, 0);
                                  if constexpr (std::same_as<T, Value>) {
                                      CHECK(value == source);
                                      CHECK_FALSE(visited);
                                      visited = true;
                                  } else {
                                      FAIL("scalar traversal changed the encoded value type");
                                  }
                              },
                              {.max_depth = 0, .strict_validation = true}));
            CHECK(visited);
        };
        check(std::uint64_t{0});
        check(std::numeric_limits<std::uint64_t>::max());
        check(negative{1});
        check(negative{std::numeric_limits<std::uint64_t>::max()});
        check(negative{0});
        check(float16_t{1.5F});
        check(float{1.5});
        check(double{-2.25});
        check(false);
        check(true);
        check(nullptr);
        check(simple{32});
        check(simple{255});
    }

    TEST_CASE("generated tag and string chunks retain balanced headers and semantic values") {
        const std::string            source_text{"hello world"};
        const std::vector<std::byte> source_bytes{std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
        for (const bool indefinite : {false, true}) {
            CAPTURE(indefinite);
            std::vector<std::byte> encoded;
            auto                   enc = make_encoder(encoded);
            REQUIRE(enc(static_tag<123>{}));
            if (indefinite) {
                REQUIRE(enc(as_indefinite{source_text}));
            } else {
                REQUIRE(enc(source_text));
            }
            if (indefinite) {
                REQUIRE(enc(as_indefinite{source_bytes}));
            } else {
                REQUIRE(enc(source_bytes));
            }
            auto dec         = make_decoder(encoded);
            using text_type  = decltype(dec.decode_text_payload(0));
            using bytes_type = decltype(dec.decode_bstring_payload(0));
            std::string                 text;
            std::vector<std::byte>      bytes;
            std::vector<semantic_frame> frames;
            bool                        tag_began = false;
            bool                        tag_ended = false;
            const auto                  visitor   = [&](const auto &value, const auto &context) {
                using T = std::remove_cvref_t<decltype(value)>;
                if constexpr (IsAnyHeader<T>) {
                    const auto frame = make_frame(value, context.depth);
                    if (context.kind == walk_event_kind::enter) {
                        CHECK_EQ(context.depth, frames.size());
                        frames.push_back(frame);
                    } else {
                        REQUIRE(context.kind == walk_event_kind::leave);
                        REQUIRE_FALSE(frames.empty());
                        CHECK(frames.back() == frame);
                        frames.pop_back();
                    }
                }
                if constexpr (std::same_as<T, as_tag_any>) {
                    CHECK_EQ(value.tag, 123);
                    CHECK_EQ(context.depth, 0);
                    if (context.kind == walk_event_kind::enter) {
                        tag_began = true;
                    } else {
                        CHECK(context.kind == walk_event_kind::leave);
                        tag_ended = true;
                    }
                } else if constexpr (std::same_as<T, text_type>) {
                    CHECK(context.kind == walk_event_kind::value);
                    REQUIRE_FALSE(frames.empty());
                    CHECK_EQ(context.depth, frames.back().depth);
                    text.append(value.begin(), value.end());
                } else if constexpr (std::same_as<T, bytes_type>) {
                    CHECK(context.kind == walk_event_kind::value);
                    REQUIRE_FALSE(frames.empty());
                    CHECK_EQ(context.depth, frames.back().depth);
                    bytes.insert(bytes.end(), value.begin(), value.end());
                }
                return status_code::success;
            };
            REQUIRE(walk_item(dec, visitor));
            CHECK(frames.empty());
            CHECK(tag_began);
            CHECK(tag_ended);
            CHECK_EQ(text, source_text);
            CHECK(bytes.empty());
            REQUIRE(walk_item(dec, visitor));
            CHECK(frames.empty());
            CHECK(bytes == source_bytes);
        }
    }

    TEST_CASE("traversal and validation use decoder tag observers and preserve resource failures") {
        const std::uint64_t              source = 7;
        const std::vector<std::uint64_t> source_tags{123, 456};
        std::vector<std::byte>           encoded;
        REQUIRE(make_encoder(encoded)(static_tag<123>{}, static_tag<456>{}, source));
        auto                       dec = make_decoder<traversal_tag_observer>(encoded);
        std::vector<std::uint64_t> callback_tags;
        bool                       value_seen = false;
        REQUIRE(walk_item(dec, [&](const auto &value, const auto &context) {
            using T = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<T, as_tag_any>) {
                if (context.kind == walk_event_kind::enter) {
                    REQUIRE_FALSE(dec.observed_tags.empty());
                    CHECK_EQ(dec.observed_tags.back(), value.tag);
                    callback_tags.push_back(value.tag);
                }
            } else if constexpr (std::same_as<T, positive>) {
                CHECK_EQ(value, source);
                value_seen = true;
            }
        }));
        CHECK(value_seen);
        CHECK(callback_tags == source_tags);
        CHECK(dec.observed_tags == source_tags);
        auto validation_dec = make_decoder<traversal_tag_observer>(encoded);
        REQUIRE(validate_item(validation_dec));
        CHECK(validation_dec.observed_tags == source_tags);

        for (const auto observer_status :
             {status_code::out_of_memory, status_code::size_limit_exceeded, status_code::incomplete, status_code::invalid_utf8_sequence}) {
            CAPTURE(static_cast<int>(observer_status));
            auto rejected_dec          = make_decoder<traversal_tag_observer>(encoded);
            rejected_dec.tag_status    = observer_status;
            bool       callback_called = false;
            const auto rejected        = walk_item(rejected_dec, [&](const auto &, const auto &) { callback_called = true; });
            REQUIRE_FALSE(rejected);
            CHECK(rejected.error() == observer_status);
            CHECK_FALSE(callback_called);
            CHECK(rejected_dec.observed_tags == std::vector<std::uint64_t>{source_tags.front()});

            auto rejected_validation_dec       = make_decoder<traversal_tag_observer>(encoded);
            rejected_validation_dec.tag_status = observer_status;
            const auto rejected_validation     = validate_item(rejected_validation_dec);
            REQUIRE_FALSE(rejected_validation);
            CHECK(rejected_validation.error() == observer_status);
            CHECK(rejected_validation_dec.observed_tags == std::vector<std::uint64_t>{source_tags.front()});
        }
    }

    TEST_CASE("visitor status is preserved at enter scalar value payload value and leave") {
        enum class rejection_point { enter, scalar_value, payload_value, leave };
        const std::array rejection_points{rejection_point::enter, rejection_point::scalar_value, rejection_point::payload_value,
                                          rejection_point::leave};
        for (const auto rejected_point : rejection_points) {
            CAPTURE(static_cast<int>(rejected_point));
            const auto             source = std::pair{std::uint64_t{7}, std::string{"text"}};
            std::vector<std::byte> encoded;
            REQUIRE(make_encoder(encoded)(source));
            auto dec            = make_decoder(encoded);
            using text_type     = decltype(dec.decode_text_payload(0));
            bool       rejected = false;
            const auto result   = walk_item(dec, [&](const auto &value, const auto &context) {
                using T = std::remove_cvref_t<decltype(value)>;
                CHECK_FALSE(rejected);
                bool reject = false;
                if constexpr (IsAnyHeader<T>) {
                    reject = (context.kind == walk_event_kind::enter && rejected_point == rejection_point::enter) ||
                             (context.kind == walk_event_kind::leave && rejected_point == rejection_point::leave);
                } else if constexpr (std::same_as<T, positive>) {
                    CHECK(context.kind == walk_event_kind::value);
                    CHECK_EQ(value, source.first);
                    reject = rejected_point == rejection_point::scalar_value;
                } else if constexpr (std::same_as<T, text_type>) {
                    CHECK(context.kind == walk_event_kind::value);
                    CHECK_EQ(value, source.second);
                    reject = rejected_point == rejection_point::payload_value;
                }
                if (reject) {
                    rejected = true;
                    return status_code::unexpected_group_size;
                }
                return status_code::success;
            });
            REQUIRE_FALSE(result);
            CHECK(result.error() == status_code::unexpected_group_size);
            CHECK(rejected);
        }
    }

    TEST_CASE("visitor exceptions are converted to public statuses by the noexcept boundary") {
        const auto check = [](auto throw_exception, status_code expected_status) {
            std::vector<std::byte> encoded;
            REQUIRE(make_encoder(encoded)(std::uint64_t{7}));
            auto       dec     = make_decoder(encoded);
            bool       visited = false;
            const auto visitor = [&](const auto &, const auto &) {
                visited = true;
                throw_exception();
            };
            static_assert(noexcept(walk_item(dec, visitor)));
            const auto result = walk_item(dec, visitor);
            REQUIRE_FALSE(result);
            CHECK(result.error() == expected_status);
            CHECK(visited);
        };
        check([] { throw std::bad_alloc{}; }, status_code::out_of_memory);
        check([] { throw std::length_error{"callback length"}; }, status_code::out_of_memory);
        check([] { throw parse_incomplete_exception{"callback input"}; }, status_code::incomplete);
        check([] { throw std::runtime_error{"callback failure"}; }, status_code::error);
        check([] { throw 42; }, status_code::error);
    }

} // TEST_SUITE("roundtrip/traversal")
