#pragma once

#include "cbor_tags/detail/smart_ptr_decode.h"
#include "cbor_tags/smart_ptr/types.h"

namespace cbor::tags::codec {

template <typename Self> struct unique_ptr : base<Self> {
    using base<Self>::decode;
    using base<Self>::encode;

    template <smart_ptr::IsUniquePointer Pointer> void encode(const Pointer &value) {
        using element_type = smart_ptr::detail::pointer_element_t<Pointer>;
        static_assert(!smart_ptr::detail::known_null_wire_v<element_type>,
                      "unique pointer cannot encode a pointee that also has a CBOR null state");
        static_assert(smart_ptr::detail::encodes_one_cbor_item<typename Self::options, element_type>(),
                      "smart pointer pointee must encode exactly one CBOR item");
        auto &enc = static_cast<Self &>(*this);
        value ? enc.encode(*value) : enc.encode(nullptr);
    }

    template <smart_ptr::IsUniquePointer Pointer>
        requires std::default_initializable<smart_ptr::detail::pointer_element_t<Pointer>>
    [[nodiscard]] status_code decode(Pointer &value, major_type major, std::byte additional_info) {
        return smart_ptr::detail::decode_unique_pointer(static_cast<Self &>(*this), value, major, additional_info);
    }

    template <typename T>
        requires smart_ptr::detail::has_pointer_null_wire_v<true, false, T>
    void encode(const std::optional<T> &) {
        static_assert(always_false<T>::value,
                      "std::optional<T> cannot contain a unique pointer null state because both empty states use CBOR null");
    }

    template <typename T>
        requires smart_ptr::detail::has_pointer_null_wire_v<true, false, T>
    [[nodiscard]] status_code decode(std::optional<T> &, major_type, std::byte) {
        static_assert(always_false<T>::value,
                      "std::optional<T> cannot contain a unique pointer null state because both empty states use CBOR null");
        return status_code::error;
    }

    template <IsVariant Variant>
        requires(smart_ptr::detail::contains_decodable_unique_pointer_v<Variant> && !smart_ptr::detail::contains_shared_pointer_v<Variant>)
    [[nodiscard]] status_code decode(Variant &value, major_type major, std::byte additional_info) {
        std::optional<std::uint64_t> tag;
        return smart_ptr::detail::decode_unique_pointer_variant(static_cast<Self &>(*this), value, major, additional_info, tag);
    }

    template <IsVariant Variant>
    [[nodiscard]] status_code decode_unique_pointer_variant_impl(Variant &value, major_type major, std::byte additional_info,
                                                                 std::optional<std::uint64_t> &tag) {
        return smart_ptr::detail::decode_unique_pointer_variant(static_cast<Self &>(*this), value, major, additional_info, tag);
    }
};

} // namespace cbor::tags::codec
