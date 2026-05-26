#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_concepts_checking.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_operators.h"
#include "cbor_tags/extensions/cbor_visualization.h"
#include "cbor_tags/extensions/custom_codec_1.h"
#include "cbor_tags/extensions/smart_ptr.h"
#include "test_util.h"

#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace tags = cbor::tags;

namespace variant_traits_test {

template <typename... Ts> struct structural_variant {
    std::variant<Ts...> storage;

    [[nodiscard]] constexpr std::size_t index() const noexcept { return storage.index(); }

    template <std::size_t I, typename... Args> constexpr decltype(auto) emplace(Args &&...args) {
        return storage.template emplace<I>(std::forward<Args>(args)...);
    }
};

template <std::size_t I, typename... Ts> constexpr decltype(auto) get(structural_variant<Ts...> &value) noexcept {
    return std::get<I>(value.storage);
}

template <std::size_t I, typename... Ts> constexpr decltype(auto) get(const structural_variant<Ts...> &value) noexcept {
    return std::get<I>(value.storage);
}

template <typename Visitor, typename... Variants> constexpr decltype(auto) visit(Visitor &&visitor, Variants &&...variants) {
    return std::visit(std::forward<Visitor>(visitor), std::forward<Variants>(variants).storage...);
}

template <typename... Ts> struct legacy_structural_variant {
    std::variant<Ts...> storage;

    [[nodiscard]] constexpr int which() const noexcept { return static_cast<int>(storage.index()); }

    template <typename U> constexpr legacy_structural_variant &operator=(U &&value) {
        storage = std::forward<U>(value);
        return *this;
    }
};

template <typename T, typename... Ts> constexpr decltype(auto) get(legacy_structural_variant<Ts...> &value) noexcept {
    return std::get<T>(value.storage);
}

template <typename T, typename... Ts> constexpr decltype(auto) get(const legacy_structural_variant<Ts...> &value) noexcept {
    return std::get<T>(value.storage);
}

template <typename Visitor, typename... Variants> constexpr decltype(auto) apply_visitor(Visitor &&visitor, Variants &&...variants) {
    return std::visit(std::forward<Visitor>(visitor), std::forward<Variants>(variants).storage...);
}

template <typename... Ts> struct manual_variant {
    std::variant<Ts...> storage;

    [[nodiscard]] constexpr std::size_t selected() const noexcept { return storage.index(); }
};

struct incomplete_traits_variant {
    std::variant<std::uint64_t, std::string> storage;
};

} // namespace variant_traits_test

namespace cbor::tags {

template <typename... Ts> struct variant_traits<variant_traits_test::manual_variant<Ts...>> {
    using variant_type = variant_traits_test::manual_variant<Ts...>;

    static constexpr std::size_t size = sizeof...(Ts);

    template <std::size_t I> using alternative = std::tuple_element_t<I, std::tuple<Ts...>>;

    [[nodiscard]] static constexpr std::size_t index(const variant_type &value) noexcept { return value.selected(); }

    template <std::size_t I, typename VariantRef> static constexpr decltype(auto) get(VariantRef &&value) {
        return std::get<I>(std::forward<VariantRef>(value).storage);
    }

    template <typename Visitor, typename... VariantRefs> static constexpr decltype(auto) visit(Visitor &&visitor, VariantRefs &&...values) {
        return std::visit(std::forward<Visitor>(visitor), std::forward<VariantRefs>(values).storage...);
    }

    template <std::size_t I, typename U> static constexpr void assign(variant_type &value, U &&decoded_value) {
        value.storage.template emplace<I>(std::forward<U>(decoded_value));
    }
};

template <> struct variant_traits<variant_traits_test::incomplete_traits_variant> {
    static constexpr std::size_t size = 2U;
};

} // namespace cbor::tags

TEST_CASE("non-std variants require explicit variant_traits opt-in") {
    using structural_variant = variant_traits_test::structural_variant<std::uint64_t, std::string>;
    using legacy_variant     = variant_traits_test::legacy_structural_variant<std::uint64_t, std::string>;
    using manual_variant     = variant_traits_test::manual_variant<std::uint64_t, std::string>;
    using incomplete_variant = variant_traits_test::incomplete_traits_variant;

    static_assert(!tags::IsVariant<structural_variant>);
    static_assert(tags::IsAggregate<structural_variant>);
    static_assert(!tags::IsVariant<legacy_variant>);
    static_assert(!tags::IsCborMajor<legacy_variant>);
    static_assert(!tags::IsVariant<incomplete_variant>);
    static_assert(!tags::IsVariant<std::tuple<std::uint64_t, std::string>>);

    static_assert(tags::IsVariant<std::variant<std::uint64_t, std::string>>);
    static_assert(tags::IsVariant<manual_variant>);
    static_assert(std::is_aggregate_v<manual_variant>);
    static_assert(tags::IsCborMajor<manual_variant>);
    static_assert(tags::valid_concept_mapping_v<manual_variant>);
    static_assert(!tags::IsAggregate<manual_variant>);

    CHECK(tags::detail::variant_size_v<manual_variant> == 2U);
}

TEST_CASE("custom variant traits roundtrip through normal CBOR") {
    using variant = variant_traits_test::manual_variant<std::uint64_t, std::string>;

    std::vector<std::byte> buffer;
    auto                   enc = tags::make_encoder(buffer);
    variant                input{std::variant<std::uint64_t, std::string>{std::string{"manual-variant"}}};
    REQUIRE(enc(input));

    auto    dec = tags::make_decoder(buffer);
    variant output;
    REQUIRE(dec(output));

    REQUIRE(tags::detail::variant_index(output) == 1U);
    CHECK(std::get<1>(output.storage) == "manual-variant");
}

TEST_CASE("custom variant traits decode nested alternatives recursively") {
    using nested_variant = variant_traits_test::manual_variant<std::uint64_t, std::string>;
    using outer_variant  = variant_traits_test::manual_variant<nested_variant, bool>;

    std::vector<std::byte> buffer;
    auto                   enc = tags::make_encoder(buffer);
    REQUIRE(enc(std::string{"nested"}));

    auto          dec = tags::make_decoder(buffer);
    outer_variant output;
    REQUIRE(dec(output));

    REQUIRE(tags::detail::variant_index(output) == 0U);
    const auto &nested = std::get<0>(output.storage);
    REQUIRE(tags::detail::variant_index(nested) == 1U);
    CHECK(std::get<1>(nested.storage) == "nested");
}

TEST_CASE("custom variant traits support operators") {
    using variant = variant_traits_test::manual_variant<std::uint64_t, std::string, std::nullptr_t>;

    const variant number{std::variant<std::uint64_t, std::string, std::nullptr_t>{std::uint64_t{7}}};
    const variant text{std::variant<std::uint64_t, std::string, std::nullptr_t>{std::string{"aa"}}};
    const variant later_text{std::variant<std::uint64_t, std::string, std::nullptr_t>{std::string{"zz"}}};
    const variant same_number{std::variant<std::uint64_t, std::string, std::nullptr_t>{std::uint64_t{7}}};

    CHECK(tags::variant_comparator<>{}(number, text));
    CHECK(tags::variant_comparator<>{}(text, later_text));
    CHECK(tags::variant_hasher{}(number) == tags::variant_hasher{}(same_number));
}

TEST_CASE("custom variant traits render CDDL") {
    using variant = variant_traits_test::manual_variant<std::uint64_t, std::string>;

    std::string schema;
    tags::cddl_schema_to<variant>(schema, {.row_options = {.format_by_rows = false}});
    CHECK_EQ(schema, "root = uint / tstr");
}

TEST_CASE("custom codec variant serialization uses custom variant traits") {
    namespace compact = tags::ext::custom_codec_1;

    using variant = variant_traits_test::manual_variant<std::uint8_t, std::string>;

    std::vector<std::byte> compact_buffer;
    auto                   enc = tags::make_encoder<compact::custom_codec_1>(compact_buffer);
    variant                input{std::variant<std::uint8_t, std::string>{std::string{"hi"}}};
    REQUIRE(enc(compact::as_custom_codec_1(tags::static_tag<77>{}, input)));

    auto    dec = tags::make_decoder<compact::custom_codec_1>(compact_buffer);
    variant output;
    REQUIRE(dec(compact::as_custom_codec_1(tags::static_tag<77>{}, output)));

    REQUIRE(tags::detail::variant_index(output) == 1U);
    CHECK(std::get<1>(output.storage) == "hi");
}

TEST_CASE("custom variant traits work with nullable smart pointer codec") {
    namespace smart = tags::ext::smart_ptr;

    using variant = variant_traits_test::manual_variant<std::shared_ptr<std::uint64_t>, std::string>;

    {
        variant input{std::variant<std::shared_ptr<std::uint64_t>, std::string>{std::make_shared<std::uint64_t>(42U)}};

        std::vector<std::byte> buffer;
        auto                   enc = tags::make_encoder<smart::nullable_ptr_codec>(buffer);
        REQUIRE(enc(input));
        CHECK_EQ(to_hex(buffer), "8201182a");

        variant output{std::variant<std::shared_ptr<std::uint64_t>, std::string>{std::string{"before"}}};
        auto    dec = tags::make_decoder<smart::nullable_ptr_codec>(buffer);
        REQUIRE(dec(output));

        REQUIRE(tags::detail::variant_index(output) == 0U);
        const auto &ptr = std::get<0>(output.storage);
        REQUIRE(static_cast<bool>(ptr));
        CHECK_EQ(*ptr, 42U);
    }

    {
        variant input{std::variant<std::shared_ptr<std::uint64_t>, std::string>{std::shared_ptr<std::uint64_t>{}}};

        std::vector<std::byte> buffer;
        auto                   enc = tags::make_encoder<smart::nullable_ptr_codec>(buffer);
        REQUIRE(enc(input));
        CHECK_EQ(to_hex(buffer), "8100");

        variant output{std::variant<std::shared_ptr<std::uint64_t>, std::string>{std::string{"before"}}};
        auto    dec = tags::make_decoder<smart::nullable_ptr_codec>(buffer);
        REQUIRE(dec(output));

        REQUIRE(tags::detail::variant_index(output) == 0U);
        CHECK_FALSE(static_cast<bool>(std::get<0>(output.storage)));
    }

    {
        variant input{std::variant<std::shared_ptr<std::uint64_t>, std::string>{std::string{"ok"}}};

        std::vector<std::byte> buffer;
        auto                   enc = tags::make_encoder<smart::nullable_ptr_codec>(buffer);
        REQUIRE(enc(input));
        CHECK_EQ(to_hex(buffer), "626f6b");

        variant output{std::variant<std::shared_ptr<std::uint64_t>, std::string>{std::shared_ptr<std::uint64_t>{}}};
        auto    dec = tags::make_decoder<smart::nullable_ptr_codec>(buffer);
        REQUIRE(dec(output));

        REQUIRE(tags::detail::variant_index(output) == 1U);
        CHECK_EQ(std::get<1>(output.storage), "ok");
    }
}

TEST_CASE("custom variant traits work with shared graph smart pointer codec") {
    namespace smart = tags::ext::smart_ptr;

    using variant = variant_traits_test::manual_variant<std::shared_ptr<std::uint64_t>, tags::static_tag<42>, std::string>;

    auto          shared = std::make_shared<std::uint64_t>(42U);
    const variant first{std::variant<std::shared_ptr<std::uint64_t>, tags::static_tag<42>, std::string>{shared}};
    const variant second{std::variant<std::shared_ptr<std::uint64_t>, tags::static_tag<42>, std::string>{shared}};

    std::vector<std::byte>             buffer;
    auto                               enc = tags::make_encoder<smart::shared_graph_codec>(buffer);
    smart::shared_graph_encode_session encode_graph;

    REQUIRE(enc(smart::as_shared_graph(encode_graph, first)));
    REQUIRE(enc(smart::as_shared_graph(encode_graph, second)));
    CHECK_EQ(to_hex(buffer), "d81c182ad81d00");

    auto                               dec = tags::make_decoder<smart::shared_graph_codec>(buffer);
    smart::shared_graph_decode_session decode_graph;
    variant decoded_first{std::variant<std::shared_ptr<std::uint64_t>, tags::static_tag<42>, std::string>{std::string{"first"}}};
    variant decoded_second{std::variant<std::shared_ptr<std::uint64_t>, tags::static_tag<42>, std::string>{std::string{"second"}}};

    REQUIRE(dec(smart::as_shared_graph(decode_graph, decoded_first)));
    REQUIRE(dec(smart::as_shared_graph(decode_graph, decoded_second)));
    REQUIRE(tags::detail::variant_index(decoded_first) == 0U);
    REQUIRE(tags::detail::variant_index(decoded_second) == 0U);

    const auto &first_ptr  = std::get<0>(decoded_first.storage);
    const auto &second_ptr = std::get<0>(decoded_second.storage);
    REQUIRE(static_cast<bool>(first_ptr));
    REQUIRE(static_cast<bool>(second_ptr));
    CHECK_EQ(*first_ptr, 42U);
    CHECK(first_ptr.get() == second_ptr.get());
}
