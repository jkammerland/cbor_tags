#pragma once

#include "cbor_tags/codec.h"
#include "cbor_tags/cwt/types.h"
#include "cbor_tags/detail/cbor_encode_error.h"
#include "cbor_tags/detail/cwt_codec.h"

namespace cbor::tags::codec {

template <typename Self> struct cwt : base<Self> {
    using base<Self>::encode;
    using base<Self>::decode;

    template <bool Borrowed> void encode(const tags::cwt::basic_header_map<Borrowed> &object) {
        namespace helpers            = tags::cwt::detail;
        auto &enc                    = static_cast<Self &>(*this);
        const auto &[alg, kid, crit] = object;

        if (helpers::validate_critical_labels(crit, alg.has_value(), kid.has_value()) != status_code::success) {
            throw tags::detail::encode_status_exception{status_code::error};
        }

        std::uint64_t size{};
        if (alg) {
            ++size;
        }
        if (!crit.empty()) {
            ++size;
        }
        if (kid) {
            ++size;
        }

        enc.encode(as_map{size});
        if (alg) {
            enc.encode(std::uint64_t{1});
            enc.encode(*alg);
        }
        if (!crit.empty()) {
            enc.encode(std::uint64_t{2});
            enc.encode(crit);
        }
        if (kid) {
            enc.encode(std::uint64_t{4});
            enc.encode(*kid);
        }
    }

    template <bool Borrowed> [[nodiscard]] status_code decode(tags::cwt::basic_header_map<Borrowed> &value) {
        auto      &dec = static_cast<Self &>(*this);
        major_type major{};
        std::byte  info{};
        const auto status = tags::detail::read_initial_byte(dec, major, info);
        return status == status_code::success ? decode(value, major, info) : status;
    }

    template <bool Borrowed>
    [[nodiscard]] status_code decode(tags::cwt::basic_header_map<Borrowed> &destination, major_type major, std::byte info) {
        if constexpr (Borrowed && !IsContiguous<typename Self::input_buffer_type>) {
            return status_code::contiguous_view_on_non_contiguous_data;
        } else {
            namespace helpers                         = tags::cwt::detail;
            auto                                 &dec = static_cast<Self &>(*this);
            tags::cwt::basic_header_map<Borrowed> decoded{};
            helpers::seen_header_labels           seen_labels;
            const auto status = helpers::decode_map_entries(dec, major, info, [&](major_type key_major, std::byte key_additional_info) {
                auto key_result = helpers::decode_header_label(dec, key_major, key_additional_info);
                if (!key_result) {
                    return key_result.error();
                }
                auto key = std::move(*key_result);
                seen_labels.add(key);

                if (helpers::is_header_label(key, 1U)) {
                    auto value = helpers::decode_algorithm(dec);
                    if (!value) {
                        return value.error();
                    }
                    decoded.alg = *value;
                } else if (helpers::is_header_label(key, 2U)) {
                    std::vector<helpers::label_storage<Borrowed>> labels;
                    const auto                                    labels_status = helpers::decode_critical_labels(dec, labels);
                    if (labels_status != status_code::success) {
                        return labels_status;
                    }
                    decoded.crit = std::move(labels);
                } else if (helpers::is_header_label(key, 4U)) {
                    helpers::byte_storage<Borrowed> value;
                    const auto                      value_status = dec.decode(value);
                    if (value_status != status_code::success) {
                        return value_status;
                    }
                    decoded.kid = std::move(value);
                } else {
                    // Unknown parameters are validated for uniqueness within this
                    // bucket, then discarded. Cross-bucket duplicate checks for
                    // unknown labels require a higher-level raw-header policy.
                    typename Self::raw_encoded_item_view ignored;
                    return dec.decode(ignored);
                }
                return status_code::success;
            });
            if (status != status_code::success) {
                return status;
            }
            if (seen_labels.has_duplicates()) {
                return status_code::error;
            }

            if (helpers::validate_critical_labels(decoded.crit, decoded.alg.has_value(), decoded.kid.has_value()) != status_code::success) {
                return status_code::error;
            }

            destination = std::move(decoded);
            return status_code::success;
        }
    }

    template <bool Borrowed> void encode(const tags::cwt::basic_claims_set<Borrowed> &object) {
        namespace helpers                                                                  = tags::cwt::detail;
        auto &enc                                                                          = static_cast<Self &>(*this);
        const auto &[issuer, subject, audience, expiration, not_before, issued_at, cwt_id] = object;

        if ((expiration && !helpers::is_finite_numeric_date(*expiration)) ||
            (not_before && !helpers::is_finite_numeric_date(*not_before)) || (issued_at && !helpers::is_finite_numeric_date(*issued_at))) {
            throw tags::detail::encode_status_exception{status_code::error};
        }

        std::uint64_t size{};
        if (issuer) {
            ++size;
        }
        if (subject) {
            ++size;
        }
        if (audience) {
            ++size;
        }
        if (expiration) {
            ++size;
        }
        if (not_before) {
            ++size;
        }
        if (issued_at) {
            ++size;
        }
        if (cwt_id) {
            ++size;
        }

        enc.encode(as_map{size});
        if (issuer) {
            enc.encode(std::uint64_t{1});
            enc.encode(*issuer);
        }
        if (subject) {
            enc.encode(std::uint64_t{2});
            enc.encode(*subject);
        }
        if (audience) {
            enc.encode(std::uint64_t{3});
            enc.encode(*audience);
        }
        if (expiration) {
            enc.encode(std::uint64_t{4});
            enc.encode(*expiration);
        }
        if (not_before) {
            enc.encode(std::uint64_t{5});
            enc.encode(*not_before);
        }
        if (issued_at) {
            enc.encode(std::uint64_t{6});
            enc.encode(*issued_at);
        }
        if (cwt_id) {
            enc.encode(std::uint64_t{7});
            enc.encode(*cwt_id);
        }
    }

    template <bool Borrowed> [[nodiscard]] status_code decode(tags::cwt::basic_claims_set<Borrowed> &value) {
        auto      &dec = static_cast<Self &>(*this);
        major_type major{};
        std::byte  info{};
        const auto status = tags::detail::read_initial_byte(dec, major, info);
        return status == status_code::success ? decode(value, major, info) : status;
    }

    template <bool Borrowed>
    [[nodiscard]] status_code decode(tags::cwt::basic_claims_set<Borrowed> &destination, major_type major, std::byte info) {
        if constexpr (Borrowed && !IsContiguous<typename Self::input_buffer_type>) {
            return status_code::contiguous_view_on_non_contiguous_data;
        } else {
            namespace helpers                         = tags::cwt::detail;
            auto                                 &dec = static_cast<Self &>(*this);
            tags::cwt::basic_claims_set<Borrowed> decoded{};
            helpers::seen_header_labels           seen_labels;
            const auto status = helpers::decode_map_entries(dec, major, info, [&](major_type key_major, std::byte key_additional_info) {
                auto key_result = helpers::decode_header_label(dec, key_major, key_additional_info);
                if (!key_result) {
                    return key_result.error();
                }
                auto label = std::move(*key_result);
                seen_labels.add(label);

                const auto *key = std::get_if<integer>(&label);
                if (key == nullptr || key->is_negative) {
                    typename Self::raw_encoded_item_view ignored;
                    return dec.decode(ignored);
                } else if (key->value == 1U) {
                    helpers::text_storage<Borrowed> value;
                    const auto                      value_status = dec.decode(value);
                    if (value_status != status_code::success) {
                        return value_status;
                    }
                    decoded.issuer = std::move(value);
                } else if (key->value == 2U) {
                    helpers::text_storage<Borrowed> value;
                    const auto                      value_status = dec.decode(value);
                    if (value_status != status_code::success) {
                        return value_status;
                    }
                    decoded.subject = std::move(value);
                } else if (key->value == 3U) {
                    helpers::text_storage<Borrowed> value;
                    const auto                      value_status = dec.decode(value);
                    if (value_status != status_code::success) {
                        return value_status;
                    }
                    decoded.audience = std::move(value);
                } else if (key->value == 4U) {
                    return helpers::decode_numeric_date_field(dec, decoded.expiration);
                } else if (key->value == 5U) {
                    return helpers::decode_numeric_date_field(dec, decoded.not_before);
                } else if (key->value == 6U) {
                    return helpers::decode_numeric_date_field(dec, decoded.issued_at);
                } else if (key->value == 7U) {
                    helpers::byte_storage<Borrowed> value;
                    const auto                      value_status = dec.decode(value);
                    if (value_status != status_code::success) {
                        return value_status;
                    }
                    decoded.cwt_id = std::move(value);
                } else {
                    typename Self::raw_encoded_item_view ignored;
                    return dec.decode(ignored);
                }
                return status_code::success;
            });
            if (status != status_code::success) {
                return status;
            }
            if (seen_labels.has_duplicates()) {
                return status_code::error;
            }

            destination = std::move(decoded);
            return status_code::success;
        }
    }

    template <bool Borrowed> void encode(const tags::cwt::basic_cose_signature<Borrowed> &object) {
        auto &enc                                              = static_cast<Self &>(*this);
        const auto &[protected_header, unprotected, signature] = object;

        enc.encode(as_array{3});
        enc.encode(protected_header);
        enc.encode(unprotected);
        enc.encode(signature);
    }

    template <bool Borrowed> [[nodiscard]] status_code decode(tags::cwt::basic_cose_signature<Borrowed> &value) {
        auto      &dec = static_cast<Self &>(*this);
        major_type major{};
        std::byte  info{};
        const auto status = tags::detail::read_initial_byte(dec, major, info);
        return status == status_code::success ? decode(value, major, info) : status;
    }

    template <bool Borrowed>
    [[nodiscard]] status_code decode(tags::cwt::basic_cose_signature<Borrowed> &destination, major_type major, std::byte info) {
        if constexpr (Borrowed && !IsContiguous<typename Self::input_buffer_type>) {
            return status_code::contiguous_view_on_non_contiguous_data;
        } else {
            namespace helpers                             = tags::cwt::detail;
            auto                                     &dec = static_cast<Self &>(*this);
            tags::cwt::basic_cose_signature<Borrowed> decoded{};
            const auto                                status =
                helpers::decode_array_fields(dec, major, info, decoded.protected_header, decoded.unprotected, decoded.signature);
            if (status != status_code::success) {
                return status;
            }
            destination = std::move(decoded);
            return status_code::success;
        }
    }

    template <bool Borrowed> void encode(const tags::cwt::basic_cose_sign<Borrowed> &object) {
        auto &enc                                                        = static_cast<Self &>(*this);
        const auto &[protected_header, unprotected, payload, signatures] = object;

        if (signatures.empty()) {
            throw tags::detail::encode_status_exception{status_code::unexpected_group_size};
        }
        enc.encode(as_array{4});
        enc.encode(protected_header);
        enc.encode(unprotected);
        enc.encode(payload);
        enc.encode(signatures);
    }

    template <bool Borrowed> [[nodiscard]] status_code decode(tags::cwt::basic_cose_sign<Borrowed> &value) {
        auto      &dec = static_cast<Self &>(*this);
        major_type major{};
        std::byte  info{};
        const auto status = tags::detail::read_initial_byte(dec, major, info);
        return status == status_code::success ? decode(value, major, info) : status;
    }

    template <bool Borrowed>
    [[nodiscard]] status_code decode(tags::cwt::basic_cose_sign<Borrowed> &destination, major_type major, std::byte info) {
        if constexpr (Borrowed && !IsContiguous<typename Self::input_buffer_type>) {
            return status_code::contiguous_view_on_non_contiguous_data;
        } else {
            namespace helpers                        = tags::cwt::detail;
            auto                                &dec = static_cast<Self &>(*this);
            tags::cwt::basic_cose_sign<Borrowed> decoded{};
            const auto status = helpers::decode_array_fields(dec, major, info, decoded.protected_header, decoded.unprotected,
                                                             decoded.payload, decoded.signatures);
            if (status != status_code::success) {
                return status;
            }
            if (decoded.signatures.empty()) {
                return status_code::unexpected_group_size;
            }
            destination = std::move(decoded);
            return status_code::success;
        }
    }

    template <bool Borrowed> void encode(const tags::cwt::basic_cose_sign1<Borrowed> &object) {
        auto &enc                                                       = static_cast<Self &>(*this);
        const auto &[protected_header, unprotected, payload, signature] = object;

        enc.encode(as_array{4});
        enc.encode(protected_header);
        enc.encode(unprotected);
        enc.encode(payload);
        enc.encode(signature);
    }

    template <bool Borrowed> [[nodiscard]] status_code decode(tags::cwt::basic_cose_sign1<Borrowed> &value) {
        auto      &dec = static_cast<Self &>(*this);
        major_type major{};
        std::byte  info{};
        const auto status = tags::detail::read_initial_byte(dec, major, info);
        return status == status_code::success ? decode(value, major, info) : status;
    }

    template <bool Borrowed>
    [[nodiscard]] status_code decode(tags::cwt::basic_cose_sign1<Borrowed> &destination, major_type major, std::byte info) {
        if constexpr (Borrowed && !IsContiguous<typename Self::input_buffer_type>) {
            return status_code::contiguous_view_on_non_contiguous_data;
        } else {
            namespace helpers                         = tags::cwt::detail;
            auto                                 &dec = static_cast<Self &>(*this);
            tags::cwt::basic_cose_sign1<Borrowed> decoded{};
            const auto status = helpers::decode_array_fields(dec, major, info, decoded.protected_header, decoded.unprotected,
                                                             decoded.payload, decoded.signature);
            if (status != status_code::success) {
                return status;
            }
            destination = std::move(decoded);
            return status_code::success;
        }
    }

    template <bool Borrowed> void encode(const tags::cwt::basic_sig_structure<Borrowed> &object) {
        auto &enc                                                                    = static_cast<Self &>(*this);
        const auto &[context, body_protected, sign_protected, external_aad, payload] = object;

        if (sign_protected) {
            enc.encode(as_array{5});
            enc.encode(context);
            enc.encode(body_protected);
            enc.encode(*sign_protected);
            enc.encode(external_aad);
            enc.encode(payload);
            return;
        }
        enc.encode(as_array{4});
        enc.encode(context);
        enc.encode(body_protected);
        enc.encode(external_aad);
        enc.encode(payload);
    }

    template <bool Borrowed> [[nodiscard]] status_code decode(tags::cwt::basic_sig_structure<Borrowed> &value) {
        auto      &dec = static_cast<Self &>(*this);
        major_type major{};
        std::byte  info{};
        const auto status = tags::detail::read_initial_byte(dec, major, info);
        return status == status_code::success ? decode(value, major, info) : status;
    }

    template <bool Borrowed>
    [[nodiscard]] status_code decode(tags::cwt::basic_sig_structure<Borrowed> &destination, major_type major, std::byte info) {
        if constexpr (Borrowed && !IsContiguous<typename Self::input_buffer_type>) {
            return status_code::contiguous_view_on_non_contiguous_data;
        } else {
            namespace helpers = tags::cwt::detail;
            auto        &dec  = static_cast<Self &>(*this);
            as_array_any array{};
            auto         header_status = dec.decode(array, major, info);
            if (header_status != status_code::success) {
                return header_status;
            }

            tags::cwt::basic_sig_structure<Borrowed> decoded{};
            if (array.size == 4U) {
                auto result = dec(decoded.context, decoded.body_protected, decoded.external_aad, decoded.payload);
                if (result) {
                    destination = std::move(decoded);
                }
                return result ? status_code::success : result.error();
            }
            if (array.size == 5U) {
                helpers::byte_storage<Borrowed> decoded_sign_protected;
                auto result = dec(decoded.context, decoded.body_protected, decoded_sign_protected, decoded.external_aad, decoded.payload);
                if (result) {
                    decoded.sign_protected = std::move(decoded_sign_protected);
                    destination            = std::move(decoded);
                }
                return result ? status_code::success : result.error();
            }
            return status_code::unexpected_group_size;
        }
    }
};

} // namespace cbor::tags::codec
