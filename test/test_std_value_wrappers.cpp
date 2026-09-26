#include "test_util.h"

#include <doctest/doctest.h>
#include <version>

#if defined(__cpp_lib_indirect) && __cpp_lib_indirect >= 202502L && defined(__cpp_lib_polymorphic) && __cpp_lib_polymorphic >= 202502L
#include "../examples/cxx26_value_wrappers.h"

#include <cbor_tags/extensions/cbor_visualization.h>
#include <cbor_tags/extensions/smart_ptr.h>
#include <cbor_tags/extensions/std_expected.h>
#include <cstddef>
#include <expected>
#include <list>
#include <memory>
#include <memory_resource>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace cbor::tags;
using cbor::tags::ext::std_indirect::std_indirect_codec;
using namespace cbor_value_example;

namespace {

struct record {
    int         id{};
    std::string name;
};

struct observing_resource : std::pmr::memory_resource {
    bool        fail{};
    std::size_t allocations{};
    void       *do_allocate(std::size_t bytes, std::size_t alignment) override {
        if (fail) {
            throw std::bad_alloc{};
        }
        ++allocations;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }
    void do_deallocate(void *pointer, std::size_t bytes, std::size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }
    bool do_is_equal(const memory_resource &other) const noexcept override { return this == &other; }
};

struct throwing_value {
    static inline bool fail{};
    int                value{};
    throwing_value() {
        if (fail) {
            throw std::runtime_error("construction failed");
        }
    }
    template <typename Coder> auto transcode(Coder &coder) { return coder(value); }
    template <typename Coder> auto encode(Coder &coder) const { return coder(value); }
};

} // namespace

TEST_CASE("std indirect uses exactly the payload wire item") {
    std::indirect<int>     value(42);
    std::vector<std::byte> encoded;
    REQUIRE(make_encoder<std_indirect_codec>(encoded)(value));
    CHECK(encoded == to_bytes("182a"));
    const auto         input = to_bytes("182a07");
    std::indirect<int> output(9);
    auto               dec = make_decoder<std_indirect_codec>(input);
    REQUIRE(dec(output));
    CHECK(*output == 42);
    int following{};
    REQUIRE(dec(following));
    CHECK(following == 7);

    const auto            structured = to_bytes("82016361626307");
    std::indirect<record> object;
    auto                  record_dec = make_decoder<std_indirect_codec>(structured);
    REQUIRE(record_dec(object));
    CHECK(object->id == 1);
    CHECK(object->name == "abc");
    REQUIRE(record_dec(following));
    CHECK(following == 7);
    encoded.clear();
    REQUIRE(make_encoder<std_indirect_codec>(encoded)(object));
    CHECK(encoded == to_bytes("820163616263"));
}

TEST_CASE("std indirect can own a null payload without becoming valueless") {
    std::indirect<std::optional<int>> output(std::optional<int>{42});
    const auto                        input = to_bytes("f607");
    auto                              dec   = make_decoder<std_indirect_codec>(input);
    REQUIRE(dec(output));
    CHECK_FALSE(output.valueless_after_move());
    CHECK_FALSE(output->has_value());
    int following{};
    REQUIRE(dec(following));
    CHECK(following == 7);
    std::vector<std::byte> encoded;
    REQUIRE(make_encoder<std_indirect_codec>(encoded)(output));
    CHECK(encoded == to_bytes("f6"));
    fmt::memory_buffer schema;
    cddl_schema_to<std::indirect<std::optional<int>>>(schema, {.row_options = {.format_by_rows = false}});
    CHECK(fmt::to_string(schema) == "root = int / null");
}

TEST_CASE("std indirect composes inside containers and with other codecs") {
    const auto                      input = to_bytes("8301020307");
    const std::list<std::byte>      linked(input.begin(), input.end());
    std::vector<std::indirect<int>> output;
    auto                            dec = make_decoder<std_indirect_codec>(linked);
    REQUIRE(dec(output));
    REQUIRE(output.size() == 3);
    CHECK(*output[0] == 1);
    CHECK(*output[1] == 2);
    CHECK(*output[2] == 3);
    int following{};
    REQUIRE(dec(following));
    CHECK(following == 7);

    using ext::std_expected::std_expected_codec;
    std::indirect<std::expected<int, std::string>> wrapped;
    const auto                                     expected_wire = to_bytes("82f5182a07");
    auto                                           expected_dec  = make_decoder<std_indirect_codec, std_expected_codec>(expected_wire);
    REQUIRE(expected_dec(wrapped));
    REQUIRE(wrapped->has_value());
    CHECK(**wrapped == 42);
    REQUIRE(expected_dec(following));
    CHECK(following == 7);
    std::vector<std::byte> encoded;
    REQUIRE(make_encoder<std_indirect_codec, std_expected_codec>(encoded)(wrapped));
    CHECK(encoded == to_bytes("82f5182a"));

    std::indirect<std::variant<int, std::string>> choice;
    const auto                                    choice_wire = to_bytes("6161");
    REQUIRE(make_decoder<std_indirect_codec>(choice_wire)(choice));
    CHECK(std::get<std::string>(*choice) == "a");
}

TEST_CASE("std indirect rejects valueless encoding and rebuilds with the destination allocator") {
    observing_resource      resource;
    std::pmr::indirect<int> source(std::allocator_arg, &resource, 42);
    auto                    owner = std::move(source);
    REQUIRE(source.valueless_after_move());
    std::vector<std::byte> encoded;
    const auto             encoded_result = make_encoder<std_indirect_codec>(encoded)(source);
    REQUIRE_FALSE(encoded_result);
    CHECK(encoded_result.error() == status_code::error);
    CHECK(encoded.empty());
    const auto input = to_bytes("0107");
    auto       dec   = make_decoder<std_indirect_codec>(input);
    REQUIRE(dec(source));
    CHECK(*source == 1);
    CHECK(source.get_allocator().resource() == &resource);
    CHECK(*owner == 42);
    int following{};
    REQUIRE(dec(following));
    CHECK(following == 7);

    const auto allocations = resource.allocations;
    const auto another     = to_bytes("02");
    REQUIRE(make_decoder<std_indirect_codec>(another)(source));
    CHECK(*source == 2);
    CHECK(resource.allocations == allocations);

    owner = std::move(source);
    REQUIRE(source.valueless_after_move());
    resource.fail     = true;
    const auto result = make_decoder<std_indirect_codec>(input)(source);
    REQUIRE_FALSE(result);
    CHECK(result.error() == status_code::out_of_memory);
    CHECK(source.valueless_after_move());
}

TEST_CASE("std indirect preserves allocator aware payloads and terminal prefixes") {
    observing_resource                        resource;
    std::pmr::indirect<std::pmr::vector<int>> output(std::allocator_arg, &resource);
    output->push_back(9);
    const auto input  = to_bytes("9f011901");
    const auto result = make_decoder<std_indirect_codec>(input)(output);
    REQUIRE_FALSE(result);
    CHECK(result.error() == status_code::incomplete);
    CHECK(*output == std::pmr::vector<int>{9, 1});
    CHECK(output->get_allocator().resource() == &resource);
    const auto         malformed = to_bytes("6161");
    std::indirect<int> number(8);
    const auto         bad_type = make_decoder<std_indirect_codec>(malformed)(number);
    REQUIRE_FALSE(bad_type);
    CHECK(bad_type.error() == status_code::no_match_for_int_on_buffer);
    CHECK(*number == 8);

    std::indirect<throwing_value> source;
    auto                          owner = std::move(source);
    REQUIRE(source.valueless_after_move());
    throwing_value::fail = true;
    const auto failure   = make_decoder<std_indirect_codec>(input)(source);
    throwing_value::fail = false;
    REQUIRE_FALSE(failure);
    CHECK(failure.error() == status_code::error);
    CHECK(source.valueless_after_move());
    CHECK(owner->value == 0);
}

TEST_CASE("polymorphic application codec preserves two concrete types and fields") {
    const auto               input = to_bytes("d9ea6a820463526578d9ea6b82634d69610907");
    auto                     dec   = make_decoder<animal_codec>(input);
    std::polymorphic<animal> first;
    std::polymorphic<animal> second;
    REQUIRE(dec(first, second));
    const auto *dog_value = dynamic_cast<const dog *>(&*first);
    const auto *cat_value = dynamic_cast<const cat *>(&*second);
    REQUIRE(dog_value);
    REQUIRE(cat_value);
    CHECK(dog_value->age == 4);
    CHECK(dog_value->name == "Rex");
    CHECK(cat_value->name == "Mia");
    CHECK(cat_value->lives == 9);
    int following{};
    REQUIRE(dec(following));
    CHECK(following == 7);
    std::vector<std::byte> encoded;
    REQUIRE(make_encoder<animal_codec>(encoded)(first, second));
    CHECK(encoded == to_bytes("d9ea6a820463526578d9ea6b82634d696109"));
}

TEST_CASE("polymorphic application codec rejects unknown and malformed items") {
    for (const auto &[wire, status] :
         {std::pair{"d9ea6c01", status_code::no_match_in_variant_on_buffer}, std::pair{"d9ea6a82046352", status_code::incomplete},
          // The wire variant collapses retriable payload mismatches into its own mismatch status.
          std::pair{"d9ea6a8204f5", status_code::no_match_in_variant_on_buffer},
          std::pair{"f6", status_code::no_match_in_variant_on_buffer}}) {
        CAPTURE(wire);
        std::polymorphic<animal> output(std::in_place_type<dog>, 8, "old");
        const auto               input  = to_bytes(wire);
        const auto               result = make_decoder<animal_codec>(input)(output);
        REQUIRE_FALSE(result);
        CHECK(result.error() == status);
        const auto *old = dynamic_cast<const dog *>(&*output);
        REQUIRE(old);
        CHECK(old->age == 8);
        CHECK(old->name == "old");
    }
    std::polymorphic<animal> unsupported;
    std::vector<std::byte>   bytes;
    const auto               result = make_encoder<animal_codec>(bytes)(unsupported);
    REQUIRE_FALSE(result);
    CHECK(result.error() == status_code::error);
    CHECK(bytes.empty());
    auto owner = std::move(unsupported);
    REQUIRE(unsupported.valueless_after_move());
    const auto moved_result = make_encoder<animal_codec>(bytes)(unsupported);
    REQUIRE_FALSE(moved_result);
    CHECK(moved_result.error() == status_code::error);
    CHECK(bytes.empty());
}

TEST_CASE("polymorphic application codec reconstructs moved values and preserves allocators") {
    observing_resource            resource;
    std::pmr::polymorphic<animal> value(std::allocator_arg, &resource, std::in_place_type<cat>, "old", 7);
    auto                          owner = std::move(value);
    REQUIRE(value.valueless_after_move());
    const auto input = to_bytes("d9ea6a820463526578");
    REQUIRE(make_decoder<animal_codec>(input)(value));
    CHECK(value.get_allocator().resource() == &resource);
    REQUIRE(dynamic_cast<const dog *>(&*value));
    const auto *old_cat = dynamic_cast<const cat *>(&*owner);
    REQUIRE(old_cat);
    CHECK(old_cat->lives == 7);
    resource.fail     = true;
    const auto result = make_decoder<animal_codec>(input)(owner);
    REQUIRE_FALSE(result);
    CHECK(result.error() == status_code::out_of_memory);
    CHECK(dynamic_cast<const cat *>(&*owner) == old_cat);
}

TEST_CASE("value wrappers compose without changing smart pointer formats and CDDL") {
    const auto                                           input = to_bytes("82d9ea6a820463526578d9ea6b82634d69610907");
    std::indirect<std::vector<std::polymorphic<animal>>> values;
    auto                                                 dec = make_decoder<std_indirect_codec, animal_codec>(input);
    REQUIRE(dec(values));
    REQUIRE(values->size() == 2);
    REQUIRE(dynamic_cast<const dog *>(&*(*values)[0]));
    REQUIRE(dynamic_cast<const cat *>(&*(*values)[1]));
    int following{};
    REQUIRE(dec(following));
    CHECK(following == 7);
    std::vector<std::byte> encoded;
    REQUIRE(make_encoder<std_indirect_codec, animal_codec>(encoded)(values));
    CHECK(encoded == to_bytes("82d9ea6a820463526578d9ea6b82634d696109"));

    using ext::smart_ptr::shared_ptr_codec;
    std::indirect<std::shared_ptr<int>> pointer(std::make_shared<int>(42));
    encoded.clear();
    REQUIRE(make_encoder<std_indirect_codec, shared_ptr_codec>(encoded)(pointer));
    CHECK(encoded == to_bytes("d81c182a"));
    std::indirect<std::shared_ptr<int>> copy;
    REQUIRE(make_decoder<std_indirect_codec, shared_ptr_codec>(encoded)(copy));
    REQUIRE(*copy);
    CHECK(**copy == 42);

    fmt::memory_buffer schema;
    cddl_schema_to<std::indirect<int>>(schema, {.row_options = {.format_by_rows = false}});
    CHECK(fmt::to_string(schema) == "root = int");
    schema.clear();
    cddl_schema_to<std::indirect<std::vector<std::polymorphic<animal>>>>(schema, {.row_options = {.format_by_rows = false}});
    CHECK(fmt::to_string(schema) == "root = [* (#6.60010([uint, tstr]) / #6.60011([tstr, uint]))]");
    schema.clear();
    cddl_schema_to<std::indirect<std::shared_ptr<int>>>(schema, {.row_options = {.format_by_rows = false}});
    CHECK(fmt::to_string(schema) == "root = null / #6.28(int) / #6.29(uint)");
}
#endif
