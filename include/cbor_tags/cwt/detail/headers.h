#pragma once

#include "cbor_tags/detail/cwt_fields.h"

namespace cbor::tags::cwt::detail {

template <class Header> auto extra_fields(Header &header) {
    if constexpr (std::same_as<std::remove_cvref_t<decltype(header.extra)>, std::monostate>) {
        return fields();
    } else {
        return cwt_fields(header.extra);
    }
}

template <class Header, class Critical> auto header_fields(Header &header, Critical &critical) {
    return fields(field<1>(header.alg), field<2>(critical), field<4>(header.kid), field<16>(header.typ), extra_fields(header));
}

template <class Label> header_label owned_label(const Label &label) {
    return std::visit(
        [](const auto &value) -> header_label {
            if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, integer>) {
                return value;
            } else {
                return std::string{value};
            }
        },
        label);
}

template <class Binding> header_label binding_label(const Binding &) {
    const auto label = Binding::label_value();
    if constexpr (std::same_as<std::remove_cv_t<decltype(label)>, integer>) {
        return label;
    } else {
        return std::string{label};
    }
}

template <class Header> auto selected_header_labels(const Header &header) {
    std::vector<header_label>                                                                       result;
    std::optional<std::span<const typename std::remove_cvref_t<decltype(header.crit)>::value_type>> critical;
    if (!header.crit.empty()) {
        critical = header.crit;
    }
    auto map = header_fields(header, critical);
    for_each_field(map, [&](const auto &binding) {
        if constexpr (field_optional<std::remove_cvref_t<decltype(*binding.value)>>) {
            if (!*binding.value) {
                return;
            }
        }
        result.push_back(binding_label(binding));
    });
    return result;
}

template <class Header> status_code validate_header_fields(const Header &header) {
    auto               labels = selected_header_labels(header);
    seen_header_labels seen;
    for (const auto &label : header.crit) {
        const auto owned = owned_label(label);
        seen.add(owned);
        if (is_header_label(owned, 2) || std::find(labels.begin(), labels.end(), owned) == labels.end()) {
            return status_code::error;
        }
    }
    return seen.has_duplicates() ? status_code::error : status_code::success;
}

template <class Header> auto received_header_labels(const Header &header) {
    auto labels = selected_header_labels(header);
    for (const auto &label : header.observed_labels) {
        if (std::find(labels.begin(), labels.end(), label) == labels.end()) {
            labels.push_back(label);
        }
    }
    return labels;
}

template <class Header> status_code validate_protected_placement(const Header &header) {
    for (const auto &label : received_header_labels(header)) {
        if (is_header_label(label, 7) || is_header_label(label, 9) || is_header_label(label, 11) || is_header_label(label, 12)) {
            return status_code::error;
        }
    }
    return validate_header_fields(header);
}

template <class Protected, class Unprotected>
status_code validate_header_pair(const Protected &protected_header, const Unprotected &unprotected) {
    if (validate_protected_placement(protected_header) != status_code::success || unprotected.alg || !unprotected.crit.empty() ||
        unprotected.typ) {
        return status_code::error;
    }
    const auto protected_labels = received_header_labels(protected_header);
    for (const auto &label : received_header_labels(unprotected)) {
        if (std::find(protected_labels.begin(), protected_labels.end(), label) != protected_labels.end()) {
            return status_code::error;
        }
    }
    return status_code::success;
}

} // namespace cbor::tags::cwt::detail
