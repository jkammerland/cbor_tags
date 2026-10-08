#include "std_value_wrapper_fixtures.h"

#if defined(__cpp_lib_indirect) && __cpp_lib_indirect >= 202502L && defined(__cpp_lib_polymorphic) && __cpp_lib_polymorphic >= 202502L
#include "std_expected_test_types.h"

using namespace cbor_value_test;

TEST_SUITE("roundtrip/std_value_wrappers") {

    TEST_CASE("std indirect round trips scalar structured and optional values") {
        const std::indirect<int>                scalar(42);
        const std::indirect<record>             object(record{1, "abc"});
        const std::indirect<std::optional<int>> empty(std::optional<int>{});
        std::vector<std::byte>                  encoded;
        REQUIRE(make_encoder<ct::codec::std_indirect>(encoded)(scalar, object, empty));
        std::indirect<int>                scalar_copy(9);
        std::indirect<record>             object_copy;
        std::indirect<std::optional<int>> empty_copy(std::optional<int>{42});
        REQUIRE(make_decoder<ct::codec::std_indirect>(encoded)(scalar_copy, object_copy, empty_copy));
        CHECK(*scalar_copy == *scalar);
        CHECK(object_copy->id == object->id);
        CHECK(object_copy->name == object->name);
        CHECK_FALSE(empty_copy.valueless_after_move());
        CHECK(*empty_copy == *empty);
    }

    TEST_CASE("std indirect composes with container expected variant and pointer values") {
        using ct::codec::shared_ptr;
        using ct::codec::std_expected;
        const std::vector<std::indirect<int>>                values{std::indirect<int>(1), std::indirect<int>(2), std::indirect<int>(3)};
        const std::indirect<std::expected<int, std::string>> wrapped(std::expected<int, std::string>{42});
        const std::indirect<std::variant<int, std::string>>  choice(std::variant<int, std::string>{"a"});
        const std::indirect<std::shared_ptr<int>>            pointer(std::make_shared<int>(42));
        std::vector<std::byte>                               encoded;
        REQUIRE(make_encoder<ct::codec::std_indirect, ct::codec::std_expected, ct::codec::shared_ptr>(encoded)(values, wrapped, choice,
                                                                                                               pointer));
        const std::list<std::byte>                     linked(encoded.begin(), encoded.end());
        std::vector<std::indirect<int>>                copies;
        std::indirect<std::expected<int, std::string>> wrapped_copy;
        std::indirect<std::variant<int, std::string>>  choice_copy;
        std::indirect<std::shared_ptr<int>>            pointer_copy;
        REQUIRE(make_decoder<ct::codec::std_indirect, ct::codec::std_expected, ct::codec::shared_ptr>(linked)(copies, wrapped_copy,
                                                                                                              choice_copy, pointer_copy));
        CHECK(copies == values);
        CHECK(*wrapped_copy == *wrapped);
        CHECK(*choice_copy == *choice);
        REQUIRE(*pointer_copy);
        CHECK(**pointer_copy == **pointer);
    }

    TEST_CASE("std indirect dispatches its payload after reading the header") {
        using ct::codec::std_expected;
        using std_expected_test::directional_item_codec;
        using std_expected_test::header_only_empty;
        const std::indirect<header_only_empty>                     original;
        const std::expected<std::indirect<header_only_empty>, int> wrapped{std::in_place};
        std::vector<std::byte>                                     encoded;
        REQUIRE(make_encoder<directional_item_codec, ct::codec::std_indirect, ct::codec::std_expected>(encoded)(original, wrapped, 7));
        std::indirect<header_only_empty>                     copy;
        std::expected<std::indirect<header_only_empty>, int> wrapped_copy;
        int                                                  following{};
        header_only_empty::decoded = 0;
        REQUIRE(
            make_decoder<directional_item_codec, ct::codec::std_indirect, ct::codec::std_expected>(encoded)(copy, wrapped_copy, following));
        CHECK_FALSE(copy.valueless_after_move());
        REQUIRE(wrapped_copy.has_value());
        CHECK_FALSE(wrapped_copy->valueless_after_move());
        CHECK(header_only_empty::decoded == 2);
        CHECK(following == 7);
    }

    TEST_CASE("pmr indirect decodes engaged immovable payloads in place") {
        observing_resource                  resource;
        std::pmr::indirect<immovable_value> value(std::allocator_arg, &resource);
        value->value                               = 9;
        auto                          *address     = &*value;
        const auto                     allocations = resource.allocations;
        std::indirect<immovable_value> original;
        original->value = 42;
        std::vector<std::byte> encoded;
        REQUIRE(make_encoder<ct::codec::std_indirect>(encoded)(original));
        REQUIRE(make_decoder<ct::codec::std_indirect>(encoded)(value));
        CHECK(value->value == original->value);
        CHECK(&*value == address);
        CHECK(value.get_allocator().resource() == &resource);
        CHECK(resource.allocations == allocations);
    }

    TEST_CASE("pmr indirect reconstructs valueless immovable payloads with the same allocator") {
        observing_resource                  resource;
        std::pmr::indirect<immovable_value> value(std::allocator_arg, &resource);
        value->value = 9;
        auto owner   = std::move(value);
        REQUIRE(value.valueless_after_move());
        const auto                     allocations = resource.allocations;
        std::indirect<immovable_value> original;
        original->value = 42;
        std::vector<std::byte> encoded;
        REQUIRE(make_encoder<ct::codec::std_indirect>(encoded)(original));
        REQUIRE(make_decoder<ct::codec::std_indirect>(encoded)(value));
        CHECK_FALSE(value.valueless_after_move());
        CHECK(value->value == original->value);
        CHECK(value.get_allocator().resource() == &resource);
        CHECK(resource.allocations == allocations + 1);
        CHECK(owner->value == 9);
        CHECK(owner.get_allocator().resource() == &resource);

        auto replacement_owner = std::move(value);
        REQUIRE(value.valueless_after_move());
        resource.fail     = true;
        const auto result = make_decoder<ct::codec::std_indirect>(encoded)(value);
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::out_of_memory);
        CHECK(value.valueless_after_move());
        CHECK(value.get_allocator().resource() == &resource);
        CHECK(replacement_owner->value == 42);
        CHECK(resource.allocations == allocations + 1);
    }

    TEST_CASE("std indirect rebuilds with the destination allocator") {
        observing_resource      resource;
        std::pmr::indirect<int> source(std::allocator_arg, &resource, 42);
        auto                    owner = std::move(source);
        REQUIRE(source.valueless_after_move());
        std::vector<std::byte> input;
        REQUIRE(make_encoder(input)(1));
        auto dec = make_decoder<ct::codec::std_indirect>(input);
        REQUIRE(dec(source));
        CHECK(*source == 1);
        CHECK(source.get_allocator().resource() == &resource);
        CHECK(*owner == 42);

        const auto             allocations = resource.allocations;
        std::vector<std::byte> another;
        REQUIRE(make_encoder(another)(2));
        REQUIRE(make_decoder<ct::codec::std_indirect>(another)(source));
        CHECK(*source == 2);
        CHECK(resource.allocations == allocations);

        owner = std::move(source);
        REQUIRE(source.valueless_after_move());
        resource.fail     = true;
        const auto result = make_decoder<ct::codec::std_indirect>(input)(source);
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::out_of_memory);
        CHECK(source.valueless_after_move());
    }

    TEST_CASE("polymorphic application codec reconstructs moved values and preserves allocators") {
        observing_resource            resource;
        std::pmr::polymorphic<animal> value(std::allocator_arg, &resource, std::in_place_type<cat>, "old", 7);
        auto                          owner = std::move(value);
        REQUIRE(value.valueless_after_move());
        const std::polymorphic<animal> original(std::in_place_type<dog>, 4, "Rex");
        std::vector<std::byte>         input;
        REQUIRE(make_encoder<cbor_value_example::codec::animal>(input)(original));
        REQUIRE(make_decoder<cbor_value_example::codec::animal>(input)(value));
        CHECK(value.get_allocator().resource() == &resource);
        REQUIRE(dynamic_cast<const dog *>(&*value));
        const auto *old_cat = dynamic_cast<const cat *>(&*owner);
        REQUIRE(old_cat);
        CHECK(old_cat->lives == 7);
        resource.fail     = true;
        const auto result = make_decoder<cbor_value_example::codec::animal>(input)(owner);
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::out_of_memory);
        CHECK(dynamic_cast<const cat *>(&*owner) == old_cat);
    }

    TEST_CASE("polymorphic values round trip concrete fields inside indirect containers") {
        std::indirect<std::vector<std::polymorphic<animal>>> values;
        values->emplace_back(std::in_place_type<dog>, 4, "Rex");
        values->emplace_back(std::in_place_type<cat>, "Mia", 9);
        std::vector<std::byte> encoded;
        REQUIRE(make_encoder<ct::codec::std_indirect, cbor_value_example::codec::animal>(encoded)(values));
        std::indirect<std::vector<std::polymorphic<animal>>> copies;
        REQUIRE(make_decoder<ct::codec::std_indirect, cbor_value_example::codec::animal>(encoded)(copies));
        REQUIRE(copies->size() == values->size());
        const auto *dog_value = dynamic_cast<const dog *>(&*(*copies)[0]);
        const auto *cat_value = dynamic_cast<const cat *>(&*(*copies)[1]);
        REQUIRE(dog_value);
        REQUIRE(cat_value);
        CHECK(dog_value->age == 4);
        CHECK(dog_value->name == "Rex");
        CHECK(cat_value->name == "Mia");
        CHECK(cat_value->lives == 9);
    }

} // TEST_SUITE
#endif
