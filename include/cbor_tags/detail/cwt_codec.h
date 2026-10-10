#pragma once

#include "cbor_tags/codec.h"
#include "cbor_tags/cwt/types.h"
#include "cbor_tags/detail/cbor_extension_decode.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace cbor::tags::cwt::detail {

template <typename Label> [[nodiscard]] constexpr bool is_header_label(const Label &label, std::uint64_t expected) {
    const auto *numeric = std::get_if<integer>(&label);
    return numeric != nullptr && !numeric->is_negative && numeric->value == expected;
}

template <typename Label = header_label> class seen_header_labels {
  public:
    constexpr void add(const Label &label) { labels_.push_back(label); }

    [[nodiscard]] constexpr bool has_duplicates() {
        sorted_indices_.resize(labels_.size());
        for (std::size_t index = 0; index < labels_.size(); ++index) {
            sorted_indices_[index] = index;
        }
        std::sort(sorted_indices_.begin(), sorted_indices_.end(),
                  [this](std::size_t lhs, std::size_t rhs) { return labels_[lhs] < labels_[rhs]; });
        for (std::size_t index = 1; index < sorted_indices_.size(); ++index) {
            if (labels_[sorted_indices_[index - 1U]] == labels_[sorted_indices_[index]]) {
                return true;
            }
        }
        return false;
    }

  private:
    // Keep labels in arrival order and sort only their indices once decoding
    // finishes. Duplicate validation is O(N log N) without moving strings.
    std::vector<Label>       labels_;
    std::vector<std::size_t> sorted_indices_;
};

template <typename Label>
[[nodiscard]] constexpr status_code validate_critical_labels(const std::vector<Label> &labels, bool has_algorithm, bool has_key_id) {
    seen_header_labels<Label> seen_labels;
    for (const auto &label : labels) {
        seen_labels.add(label);

        if (is_header_label(label, 1U)) {
            if (!has_algorithm) {
                return status_code::error;
            }
        } else if (is_header_label(label, 4U)) {
            if (!has_key_id) {
                return status_code::error;
            }
        } else {
            return status_code::error;
        }
    }
    return seen_labels.has_duplicates() ? status_code::error : status_code::success;
}

// Core wrap_as_array requires a definite length. COSE also accepts indefinite
// envelopes, with exact field counts, terminal errors and a required closing break.
// Keep this semantic check local rather than changing core group decoding.
template <typename Decoder, typename DecodeEntry>
[[nodiscard]] constexpr status_code decode_group_entries(Decoder &dec, major_type major, std::byte additional_info,
                                                         major_type expected_major, status_code major_mismatch, DecodeEntry &&decode_entry,
                                                         std::optional<std::uint64_t> expected_size = std::nullopt) {
    auto status = status_code::success;
    if (major != expected_major) {
        return major_mismatch;
    }

    const auto decode_next = [&]() {
        major_type key_major{};
        std::byte  key_additional_info{};
        const auto key_status = tags::detail::read_initial_byte(dec, key_major, key_additional_info);
        if (key_status != status_code::success) {
            return key_status;
        }
        return decode_entry(key_major, key_additional_info);
    };

    if (additional_info != std::byte{31}) {
        std::uint64_t size{};
        status = tags::detail::decode_unsigned_argument(dec, additional_info, size);
        if (status != status_code::success) {
            return status;
        }
        if (expected_size && size != *expected_size) {
            return status_code::unexpected_group_size;
        }
        for (std::uint64_t index = 0; index < size; ++index) {
            status = decode_next();
            if (status != status_code::success) {
                return status;
            }
        }
        return status_code::success;
    }

    while (true) {
        major_type key_major{};
        std::byte  key_additional_info{};
        status = tags::detail::read_initial_byte(dec, key_major, key_additional_info);
        if (status != status_code::success) {
            return status;
        }
        if (key_major == major_type::Simple && key_additional_info == std::byte{31}) {
            return status_code::success;
        }
        status = decode_entry(key_major, key_additional_info);
        if (status != status_code::success) {
            return status;
        }
    }
}

template <typename Decoder, typename DecodeEntry>
[[nodiscard]] constexpr status_code decode_map_entries(Decoder &dec, major_type major, std::byte info, DecodeEntry &&decode_entry) {
    return decode_group_entries(dec, major, info, major_type::Map, status_code::no_match_for_map_on_buffer,
                                std::forward<DecodeEntry>(decode_entry));
}

template <typename Decoder, typename... Fields>
[[nodiscard]] constexpr status_code decode_array_fields(Decoder &dec, major_type initial_major, std::byte initial_info, Fields &...fields) {
    std::size_t index{};
    const auto  decode_entry = [&](major_type major, std::byte additional_info) {
        auto        status = status_code::unexpected_group_size;
        std::size_t field_index{};
        const auto  decode_field = [&](auto &field) {
            if (field_index == index) {
                status = dec.decode(field, major, additional_info);
            }
            ++field_index;
        };
        (decode_field(fields), ...);
        if (status == status_code::success) {
            ++index;
        }
        return status;
    };
    const auto status = decode_group_entries(dec, initial_major, initial_info, major_type::Array, status_code::no_match_for_array_on_buffer,
                                             decode_entry, sizeof...(Fields));
    if (status != status_code::success) {
        return status;
    }
    return index == sizeof...(Fields) ? status_code::success : status_code::unexpected_group_size;
}

template <typename Decoder> [[nodiscard]] constexpr expected<algorithm, status_code> decode_algorithm(Decoder &dec) {
    major_type major{};
    std::byte  additional_info{};
    const auto header_status = tags::detail::read_initial_byte(dec, major, additional_info);
    if (header_status != status_code::success) {
        return unexpected<status_code>{header_status};
    }
    if (major != major_type::UnsignedInteger && major != major_type::NegativeInteger) {
        return unexpected<status_code>{status_code::error};
    }

    std::uint64_t argument{};
    const auto    status = tags::detail::decode_unsigned_argument(dec, additional_info, argument);
    if (status != status_code::success) {
        return unexpected<status_code>{status};
    }
    constexpr auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (argument > maximum) {
        return unexpected<status_code>{status_code::error};
    }

    const auto value =
        major == major_type::UnsignedInteger ? static_cast<std::int64_t>(argument) : std::int64_t{-1} - static_cast<std::int64_t>(argument);
    return static_cast<algorithm>(value);
}

template <typename Label = header_label, typename Decoder>
[[nodiscard]] expected<Label, status_code> decode_header_label(Decoder &dec, major_type major, std::byte additional_info) {
    if (major == major_type::UnsignedInteger) {
        std::uint64_t value{};
        const auto    status = tags::detail::decode_unsigned_argument(dec, additional_info, value);
        if (status != status_code::success) {
            return unexpected<status_code>{status};
        }
        return Label{integer{value}};
    }
    if (major == major_type::NegativeInteger) {
        std::uint64_t argument{};
        const auto    status = tags::detail::decode_unsigned_argument(dec, additional_info, argument);
        if (status != status_code::success) {
            return unexpected<status_code>{status};
        }
        return Label{integer{negative{argument + 1U}}};
    }
    if (major == major_type::TextString) {
        std::variant_alternative_t<1, Label> value;
        const auto                           status = dec.decode(value, major, additional_info);
        if (status != status_code::success) {
            return unexpected<status_code>{status};
        }
        return Label{std::move(value)};
    }
    return unexpected<status_code>{status_code::error};
}

template <typename Label = header_label, typename Decoder> [[nodiscard]] expected<Label, status_code> decode_header_label(Decoder &dec) {
    major_type major{};
    std::byte  additional_info{};
    const auto status = tags::detail::read_initial_byte(dec, major, additional_info);
    if (status != status_code::success) {
        return unexpected<status_code>{status};
    }
    return decode_header_label<Label>(dec, major, additional_info);
}

template <typename Decoder, typename Label> [[nodiscard]] status_code decode_critical_labels(Decoder &dec, std::vector<Label> &labels) {
    major_type major{};
    std::byte  info{};
    const auto header_status = tags::detail::read_initial_byte(dec, major, info);
    if (header_status != status_code::success)
        return header_status;
    const auto status = decode_group_entries(dec, major, info, major_type::Array, status_code::no_match_for_array_on_buffer,
                                             [&](major_type label_major, std::byte label_additional_info) {
                                                 auto label = decode_header_label<Label>(dec, label_major, label_additional_info);
                                                 if (!label) {
                                                     return label.error();
                                                 }
                                                 labels.push_back(std::move(*label));
                                                 return status_code::success;
                                             });
    return status == status_code::success && labels.empty() ? status_code::error : status;
}

template <typename Decoder>
[[nodiscard]] expected<numeric_date, status_code> decode_numeric_date(Decoder &dec, major_type major, std::byte additional_info) {
    if (major == major_type::UnsignedInteger || major == major_type::NegativeInteger) {
        std::uint64_t argument{};
        const auto    status = tags::detail::decode_unsigned_argument(dec, additional_info, argument);
        if (status != status_code::success) {
            return unexpected<status_code>{status};
        }

        constexpr auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (argument > maximum) {
            return unexpected<status_code>{status_code::no_match_for_int_on_buffer};
        }
        const auto value = major == major_type::UnsignedInteger ? static_cast<std::int64_t>(argument)
                                                                : std::int64_t{-1} - static_cast<std::int64_t>(argument);
        return numeric_date{value};
    }

    if (major == major_type::Simple) {
        double     value{};
        const auto status = dec.decode(value, major, additional_info);
        if (status != status_code::success) {
            return unexpected<status_code>{status};
        }
        if (!std::isfinite(value)) {
            return unexpected<status_code>{status_code::error};
        }
        return numeric_date{value};
    }

    return unexpected<status_code>{status_code::no_match_in_variant_on_buffer};
}

template <typename Decoder> [[nodiscard]] status_code decode_numeric_date_field(Decoder &dec, std::optional<numeric_date> &field) {
    major_type major{};
    std::byte  additional_info{};
    const auto status = tags::detail::read_initial_byte(dec, major, additional_info);
    if (status != status_code::success) {
        return status;
    }
    auto value = decode_numeric_date(dec, major, additional_info);
    if (!value) {
        return value.error();
    }
    field = std::move(*value);
    return status_code::success;
}

[[nodiscard]] inline bool is_finite_numeric_date(const numeric_date &value) {
    const auto *floating = std::get_if<double>(&value);
    return floating == nullptr || std::isfinite(*floating);
}

} // namespace cbor::tags::cwt::detail
