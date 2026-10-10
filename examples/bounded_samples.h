#pragma once

#include <cbor_tags/codec.h>
#include <cstddef>
#include <type_traits>
#include <vector>

namespace app {

namespace ct = cbor::tags;

struct samples {
    std::vector<int> values;
};

namespace codec {

template <typename Self> struct samples : ct::codec::base<Self> {
    using base = ct::codec::base<Self>;
    using base::decode;
    using base::encode;

    template <typename Value, std::size_t Min, std::size_t Max>
        requires std::same_as<std::remove_cvref_t<Value>, app::samples>
    void encode(const ct::bounded_size<Value, Min, Max> &bounded) {
        static_cast<Self &>(*this).encode(ct::as_bounded_size<Min, Max>(bounded.value().values));
    }

    template <typename Value, std::size_t Min, std::size_t Max>
        requires std::same_as<std::remove_cvref_t<Value>, app::samples>
    ct::status_code decode(ct::bounded_size<Value, Min, Max> &bounded, ct::major_type major, std::byte additional_info) {
        auto values = ct::as_bounded_size<Min, Max>(bounded.value().values);
        return static_cast<Self &>(*this).decode(values, major, additional_info);
    }

    template <typename Value>
        requires std::same_as<std::remove_cvref_t<Value>, app::samples>
    void encode(const ct::dynamic_bounded_size<Value> &bounded) {
        static_cast<Self &>(*this).encode(ct::as_bounded_size(bounded.value().values, bounded.min_size(), bounded.max_size()));
    }

    template <typename Value>
        requires std::same_as<std::remove_cvref_t<Value>, app::samples>
    ct::status_code decode(ct::dynamic_bounded_size<Value> &bounded, ct::major_type major, std::byte additional_info) {
        auto values = ct::as_bounded_size(bounded.value().values, bounded.min_size(), bounded.max_size());
        return static_cast<Self &>(*this).decode(values, major, additional_info);
    }
};

} // namespace codec
} // namespace app
