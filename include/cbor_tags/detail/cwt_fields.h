#pragma once

#include "cbor_tags/cwt/fields.h"
#include "cbor_tags/detail/cwt_codec.h"

#include <array>

namespace cbor::tags::cwt::detail {

template <typename Encoder, typename Map> void encode_fields(Encoder &enc, const Map &map) {
    std::uint64_t size{};
    for_each_field(map, [&](const auto &binding) {
        if constexpr (field_optional<std::remove_cvref_t<decltype(*binding.value)>>) {
            size += binding.value->has_value();
        } else {
            ++size;
        }
    });

    enc.encode(as_map{size});
    for_each_field(map, [&](const auto &binding) {
        using Binding = std::remove_cvref_t<decltype(binding)>;
        if constexpr (field_optional<std::remove_cvref_t<decltype(*binding.value)>>) {
            if (*binding.value) {
                enc.encode(Binding::label_value());
                enc.encode(**binding.value);
            }
        } else {
            enc.encode(Binding::label_value());
            enc.encode(*binding.value);
        }
    });
}

template <typename Decoder, typename Observer, typename ReadValue, typename... Bindings>
status_code decode_fields(Decoder &dec, field_map<Bindings...> &map, major_type major, std::byte info, Observer &&observe,
                          ReadValue &&read_value) try {
    std::array<bool, sizeof...(Bindings)> present{};
    seen_header_labels                    seen;

    const auto status = decode_map_entries(dec, major, info, [&](major_type key_major, std::byte key_info) {
        auto label = decode_header_label(dec, key_major, key_info);
        if (!label) {
            return label.error();
        }
        seen.add(*label);
        const auto observed = observe(*label);
        if (observed != status_code::success) {
            return observed;
        }

        bool        matched{};
        auto        result = status_code::success;
        std::size_t index{};
        for_each_field(map, [&](auto &binding) {
            using Binding             = std::remove_cvref_t<decltype(binding)>;
            using Value               = std::remove_cvref_t<decltype(*binding.value)>;
            const auto expected_label = Binding::label_value();
            bool       same{};
            if constexpr (std::same_as<std::remove_cv_t<decltype(expected_label)>, integer>) {
                const auto *number = std::get_if<integer>(&*label);
                same               = number && *number == expected_label;
            } else {
                const auto *text = std::get_if<std::string>(&*label);
                same             = text && *text == expected_label;
            }
            if (same) {
                matched        = true;
                present[index] = true;
                if constexpr (field_optional<Value>) {
                    typename Value::value_type decoded{};
                    result = read_value(*label, decoded);
                    if (result == status_code::success) {
                        *binding.value = std::move(decoded);
                    }
                } else {
                    result = read_value(*label, *binding.value);
                }
            }
            ++index;
        });
        if (!matched) {
            typename Decoder::encoded_item_view ignored;
            return dec.decode(ignored);
        }
        return result;
    });
    if (status != status_code::success) {
        return status;
    }
    if (seen.has_duplicates()) {
        return status_code::error;
    }

    bool        missing{};
    std::size_t index{};
    for_each_field(map, [&](auto &binding) {
        if constexpr (!field_optional<std::remove_cvref_t<decltype(*binding.value)>>) {
            missing |= !present[index];
        }
        ++index;
    });
    if (missing) {
        return status_code::error;
    }

    index = 0;
    for_each_field(map, [&](auto &binding) {
        if constexpr (field_optional<std::remove_cvref_t<decltype(*binding.value)>>) {
            if (!present[index]) {
                binding.value->reset();
            }
        }
        ++index;
    });
    return status_code::success;
} catch (const tags::detail::decode_status_exception &error) { return error.status; } catch (const tags::parse_incomplete_exception &) {
    return status_code::incomplete;
}

template <typename Decoder, typename Observer, typename... Bindings>
status_code decode_fields(Decoder &dec, field_map<Bindings...> &map, major_type major, std::byte info, Observer &&observe) {
    return decode_fields(dec, map, major, info, std::forward<Observer>(observe),
                         [&](const header_label &, auto &value) { return dec.decode(value); });
}

template <typename Decoder, typename... Bindings>
status_code decode_fields(Decoder &dec, field_map<Bindings...> &map, major_type major, std::byte info) {
    return decode_fields(dec, map, major, info, [](const header_label &) { return status_code::success; });
}

} // namespace cbor::tags::cwt::detail
