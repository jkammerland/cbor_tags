#pragma once

#include <cbor_tags/extensions/std_indirect.h>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <variant>

#if !defined(__cpp_lib_polymorphic) || __cpp_lib_polymorphic < 202502L
#error "This example requires C++26 std::polymorphic"
#endif

namespace cbor_value_example {

namespace ct = cbor::tags;

struct animal {
    virtual ~animal() = default;
};

struct dog final : animal {
    std::uint64_t age;
    std::string   name;
    dog(std::uint64_t age_value, std::string name_value) : age(age_value), name(std::move(name_value)) {}
};

struct cat final : animal {
    std::string   name;
    std::uint64_t lives;
    cat(std::string name_value, std::uint64_t lives_value) : name(std::move(name_value)), lives(lives_value) {}
};

using dog_wire    = std::tuple<ct::static_tag<60010>, std::uint64_t, std::string>;
using cat_wire    = std::tuple<ct::static_tag<60011>, std::string, std::uint64_t>;
using animal_wire = std::variant<dog_wire, cat_wire>;

template <typename T> struct is_animal_value : std::false_type {};
template <typename Alloc> struct is_animal_value<std::polymorphic<animal, Alloc>> : std::true_type {};

template <typename T> consteval bool has_animal_alternative() {
    if constexpr (is_animal_value<T>::value) {
        return true;
    } else if constexpr (ct::IsOptional<T>) {
        return has_animal_alternative<typename T::value_type>();
    } else if constexpr (ct::IsVariant<T>) {
        return ct::detail::with_variant_alternatives<T>([]<typename... Ts>() { return (has_animal_alternative<Ts>() || ...); });
    } else {
        return false;
    }
}

template <typename Self> struct animal_codec : ct::codec_mixin_base<Self> {
    using base = ct::codec_mixin_base<Self>;
    using base::decode;
    using base::encode;

    template <typename Alloc> void encode(const std::polymorphic<animal, Alloc> &value) {
        static_assert(Self::options::wrap_groups, "animal_codec requires wrapped tagged payload groups");
        if (value.valueless_after_move()) {
            throw ct::detail::encode_status_exception{ct::status_code::error};
        }
        auto &enc = static_cast<Self &>(*this);
        if (const auto *dog_value = dynamic_cast<const dog *>(&*value)) {
            enc.encode(dog_wire{{}, dog_value->age, dog_value->name});
        } else if (const auto *cat_value = dynamic_cast<const cat *>(&*value)) {
            enc.encode(cat_wire{{}, cat_value->name, cat_value->lives});
        } else {
            throw ct::detail::encode_status_exception{ct::status_code::error};
        }
    }

    template <typename Alloc>
    [[nodiscard]] ct::status_code decode(std::polymorphic<animal, Alloc> &value, ct::major_type major, std::byte additional_info) {
        static_assert(Self::options::wrap_groups, "animal_codec requires wrapped tagged payload groups");
        animal_wire wire;
        const auto  status = static_cast<Self &>(*this).decode(wire, major, additional_info);
        if (status != ct::status_code::success) {
            return status;
        }
        // Decode the complete wire item before constructing the replacement.
        std::visit(
            [&value](auto &&concrete) {
                using wire_type = std::remove_cvref_t<decltype(concrete)>;
                if constexpr (std::same_as<wire_type, dog_wire>) {
                    value = std::polymorphic<animal, Alloc>(std::allocator_arg, value.get_allocator(), std::in_place_type<dog>,
                                                            std::get<1>(concrete), std::move(std::get<2>(concrete)));
                } else {
                    value = std::polymorphic<animal, Alloc>(std::allocator_arg, value.get_allocator(), std::in_place_type<cat>,
                                                            std::move(std::get<1>(concrete)), std::get<2>(concrete));
                }
            },
            std::move(wire));
        return ct::status_code::success;
    }

    template <ct::IsVariant Variant>
        requires(has_animal_alternative<Variant>())
    void encode(const Variant &) {
        static_assert(ct::always_false<Variant>::value, "polymorphic variant alternatives require the explicit animal_wire variant");
    }

    template <ct::IsVariant Variant>
        requires(has_animal_alternative<Variant>())
    [[nodiscard]] ct::status_code decode(Variant &, ct::major_type, std::byte) {
        static_assert(ct::always_false<Variant>::value, "polymorphic variant alternatives require the explicit animal_wire variant");
        return ct::status_code::error;
    }
};

} // namespace cbor_value_example

namespace cbor::tags::cddl {

template <typename Alloc> struct wire_type<std::polymorphic<cbor_value_example::animal, Alloc>> {
    using type = cbor_value_example::animal_wire;
};

} // namespace cbor::tags::cddl
