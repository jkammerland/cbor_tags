#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_concepts_checking.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_operators.h"
#include "cbor_tags/extensions/cbor_visualization.h"
#include "cbor_tags/extensions/custom_codec_1.h"
#include "test_util.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#if __has_include(<boost/variant.hpp>)
#include <boost/variant.hpp>
#define CBOR_TAGS_TEST_HAS_BOOST_VARIANT 1
#endif

#if __has_include(<boost/variant2/variant.hpp>)
#include <boost/variant2/variant.hpp>
#define CBOR_TAGS_TEST_HAS_BOOST_VARIANT2 1
#endif

namespace tags = cbor::tags;

namespace variant_like_test {

template <typename... Ts> struct adl_variant {
    std::variant<Ts...> storage;

    constexpr adl_variant() = default;

    template <typename T>
        requires(!std::same_as<std::remove_cvref_t<T>, adl_variant> && std::constructible_from<std::variant<Ts...>, T>)
    constexpr adl_variant(T &&value) : storage(std::forward<T>(value)) {}

    template <typename T>
        requires(!std::same_as<std::remove_cvref_t<T>, adl_variant> && std::assignable_from<std::variant<Ts...> &, T>)
    constexpr adl_variant &operator=(T &&value) {
        storage = std::forward<T>(value);
        return *this;
    }

    [[nodiscard]] constexpr std::size_t index() const noexcept { return storage.index(); }

    template <std::size_t I, typename... Args> constexpr decltype(auto) emplace(Args &&...args) {
        return storage.template emplace<I>(std::forward<Args>(args)...);
    }
};

template <std::size_t I, typename... Ts> constexpr decltype(auto) get(adl_variant<Ts...> &value) noexcept {
    return std::get<I>(value.storage);
}

template <std::size_t I, typename... Ts> constexpr decltype(auto) get(const adl_variant<Ts...> &value) noexcept {
    return std::get<I>(value.storage);
}

template <typename T>
concept HasVariantStorage = requires(T &&value) { std::forward<T>(value).storage; };

template <typename T>
concept HasIndexMember = requires(const std::remove_cvref_t<T> &value) {
    { value.index() } -> std::convertible_to<std::size_t>;
};

template <typename Visitor, typename... Variants>
    requires((HasVariantStorage<Variants> && HasIndexMember<Variants>) && ...)
constexpr decltype(auto) visit(Visitor &&visitor, Variants &&...variants) {
    return std::visit(std::forward<Visitor>(visitor), std::forward<Variants>(variants).storage...);
}

template <typename... Ts> struct which_variant {
    std::variant<Ts...> storage;

    constexpr which_variant() = default;

    template <typename T>
        requires(!std::same_as<std::remove_cvref_t<T>, which_variant> && std::constructible_from<std::variant<Ts...>, T>)
    constexpr which_variant(T &&value) : storage(std::forward<T>(value)) {}

    template <typename T>
        requires(!std::same_as<std::remove_cvref_t<T>, which_variant> && std::assignable_from<std::variant<Ts...> &, T>)
    constexpr which_variant &operator=(T &&value) {
        storage = std::forward<T>(value);
        return *this;
    }

    [[nodiscard]] constexpr int which() const noexcept { return static_cast<int>(storage.index()); }
};

template <typename T, typename... Ts> constexpr decltype(auto) get(which_variant<Ts...> &value) noexcept {
    return std::get<T>(value.storage);
}

template <typename T, typename... Ts> constexpr decltype(auto) get(const which_variant<Ts...> &value) noexcept {
    return std::get<T>(value.storage);
}

template <typename Visitor, typename... Variants>
    requires(HasVariantStorage<Variants> && ...)
constexpr decltype(auto) apply_visitor(Visitor &&visitor, Variants &&...variants) {
    return std::visit(std::forward<Visitor>(visitor), std::forward<Variants>(variants).storage...);
}

template <typename... Ts> struct aggregate_adl_variant {
    std::variant<Ts...> storage;

    [[nodiscard]] constexpr std::size_t index() const noexcept { return storage.index(); }

    template <std::size_t I, typename... Args> constexpr decltype(auto) emplace(Args &&...args) {
        return storage.template emplace<I>(std::forward<Args>(args)...);
    }
};

template <std::size_t I, typename... Ts> constexpr decltype(auto) get(aggregate_adl_variant<Ts...> &value) noexcept {
    return std::get<I>(value.storage);
}

template <std::size_t I, typename... Ts> constexpr decltype(auto) get(const aggregate_adl_variant<Ts...> &value) noexcept {
    return std::get<I>(value.storage);
}

template <typename... Ts> struct indexed_aggregate {
    std::size_t selected{};

    [[nodiscard]] constexpr std::size_t index() const noexcept { return selected; }
};

} // namespace variant_like_test

TEST_CASE("variant-like traits recognize non-std variant APIs") {
    using adl_variant       = variant_like_test::adl_variant<std::uint64_t, std::string>;
    using aggregate_variant = variant_like_test::aggregate_adl_variant<std::uint64_t, std::string>;
    using indexed_aggregate = variant_like_test::indexed_aggregate<std::uint64_t, std::string>;
    using which_variant     = variant_like_test::which_variant<std::uint64_t, std::string>;

    static_assert(tags::IsVariant<adl_variant>);
    static_assert(tags::IsVariant<aggregate_variant>);
    static_assert(tags::IsVariant<which_variant>);
    static_assert(tags::IsCborMajor<adl_variant>);
    static_assert(tags::IsCborMajor<aggregate_variant>);
    static_assert(tags::IsCborMajor<which_variant>);
    static_assert(tags::valid_concept_mapping_v<adl_variant>);
    static_assert(tags::valid_concept_mapping_v<aggregate_variant>);
    static_assert(tags::valid_concept_mapping_v<which_variant>);
    static_assert(!tags::IsVariant<indexed_aggregate>);
    static_assert(std::is_aggregate_v<aggregate_variant>);
    static_assert(!tags::IsAggregate<aggregate_variant>);
    static_assert(!tags::IsVariant<std::tuple<std::uint64_t, std::string>>);

    CHECK(tags::detail::variant_size_v<adl_variant> == 2U);
    CHECK(tags::detail::variant_size_v<aggregate_variant> == 2U);
    CHECK(tags::detail::variant_size_v<which_variant> == 2U);
}

TEST_CASE("variant-like type with ADL visit and indexed get roundtrips through normal CBOR") {
    using variant = variant_like_test::adl_variant<std::uint64_t, std::string>;

    std::vector<std::byte> buffer;
    auto                   enc = tags::make_encoder(buffer);
    variant                input{std::string{"variant-like"}};
    REQUIRE(enc(input));

    auto    dec = tags::make_decoder(buffer);
    variant output;
    REQUIRE(dec(output));

    REQUIRE(tags::detail::variant_index(output) == 1U);
    CHECK(variant_like_test::get<1>(output) == "variant-like");
}

TEST_CASE("aggregate variant-like wrapper roundtrips as a variant instead of an aggregate") {
    using variant = variant_like_test::aggregate_adl_variant<std::uint64_t, std::string>;

    std::vector<std::byte> buffer;
    auto                   enc = tags::make_encoder(buffer);
    variant                input{std::variant<std::uint64_t, std::string>{std::string{"aggregate-variant"}}};
    REQUIRE(enc(input));

    auto    dec = tags::make_decoder(buffer);
    variant output;
    REQUIRE(dec(output));

    REQUIRE(tags::detail::variant_index(output) == 1U);
    CHECK(variant_like_test::get<1>(output) == "aggregate-variant");
}

TEST_CASE("variant-like type with which and apply_visitor uses assignment fallback") {
    using variant = variant_like_test::which_variant<std::uint64_t, std::string>;

    std::vector<std::byte> buffer;
    auto                   enc = tags::make_encoder(buffer);
    variant                input{std::uint64_t{42}};
    REQUIRE(enc(input));

    auto    dec = tags::make_decoder(buffer);
    variant output;
    REQUIRE(dec(output));

    REQUIRE(tags::detail::variant_index(output) == 0U);
    CHECK(variant_like_test::get<std::uint64_t>(output) == 42U);
}

TEST_CASE("nested variant-like alternatives decode recursively") {
    using nested_variant = variant_like_test::which_variant<std::uint64_t, std::string>;
    using outer_variant  = variant_like_test::adl_variant<nested_variant, bool>;

    std::vector<std::byte> buffer;
    auto                   enc = tags::make_encoder(buffer);
    REQUIRE(enc(std::string{"nested"}));

    auto          dec = tags::make_decoder(buffer);
    outer_variant output;
    REQUIRE(dec(output));

    REQUIRE(tags::detail::variant_index(output) == 0U);
    const auto &nested = variant_like_test::get<0>(output);
    REQUIRE(tags::detail::variant_index(nested) == 1U);
    CHECK(variant_like_test::get<std::string>(nested) == "nested");
}

TEST_CASE("variant-like operators use the variant traits hooks") {
    using variant = variant_like_test::adl_variant<std::uint64_t, std::string, std::nullptr_t>;

    const variant number{std::uint64_t{7}};
    const variant text{std::string{"text"}};
    const variant same_number{std::uint64_t{7}};

    CHECK(tags::variant_comparator<>{}(number, text));
    CHECK(tags::variant_hasher{}(number) == tags::variant_hasher{}(same_number));
}

TEST_CASE("variant-like CDDL rendering uses the alternative list") {
    using variant = variant_like_test::adl_variant<std::uint64_t, std::string>;

    std::string schema;
    tags::cddl_schema_to<variant>(schema, {.row_options = {.format_by_rows = false}});
    CHECK_EQ(schema, "root = uint / tstr");
}

TEST_CASE("custom codec variant serialization uses variant-like traits") {
    namespace compact = tags::ext::custom_codec_1;

    using variant = variant_like_test::which_variant<std::uint8_t, std::string>;

    std::vector<std::byte> compact_buffer;
    auto                   enc = tags::make_encoder<compact::custom_codec_1>(compact_buffer);
    variant                input{std::string{"hi"}};
    REQUIRE(enc(compact::as_custom_codec_1(tags::static_tag<77>{}, input)));

    auto    dec = tags::make_decoder<compact::custom_codec_1>(compact_buffer);
    variant output;
    REQUIRE(dec(compact::as_custom_codec_1(tags::static_tag<77>{}, output)));

    REQUIRE(tags::detail::variant_index(output) == 1U);
    CHECK(variant_like_test::get<std::string>(output) == "hi");
}

#if CBOR_TAGS_TEST_HAS_BOOST_VARIANT
TEST_CASE("boost variant roundtrips through variant-like traits when available") {
    using variant = boost::variant<std::uint64_t, std::string>;

    static_assert(tags::IsVariant<variant>);
    static_assert(tags::valid_concept_mapping_v<variant>);

    std::vector<std::byte> buffer;
    auto                   enc = tags::make_encoder(buffer);
    variant                input{std::string{"boost"}};
    REQUIRE(enc(input));

    auto    dec = tags::make_decoder(buffer);
    variant output;
    REQUIRE(dec(output));

    REQUIRE(tags::detail::variant_index(output) == 1U);
    CHECK(boost::get<std::string>(output) == "boost");
}
#endif

#if CBOR_TAGS_TEST_HAS_BOOST_VARIANT2
TEST_CASE("boost variant2 roundtrips through variant-like traits when available") {
    using variant = boost::variant2::variant<std::uint64_t, std::string>;

    static_assert(tags::IsVariant<variant>);
    static_assert(tags::valid_concept_mapping_v<variant>);

    std::vector<std::byte> buffer;
    auto                   enc = tags::make_encoder(buffer);
    variant                input{std::string{"boost2"}};
    REQUIRE(enc(input));

    auto    dec = tags::make_decoder(buffer);
    variant output;
    REQUIRE(dec(output));

    REQUIRE(tags::detail::variant_index(output) == 1U);
    CHECK(boost::variant2::get<std::string>(output) == "boost2");
}
#endif
