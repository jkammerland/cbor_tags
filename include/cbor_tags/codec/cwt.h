#pragma once

#include "cbor_tags/codec.h"
#include "cbor_tags/cwt/detail/headers.h"
#include "cbor_tags/cwt/types.h"
#include "cbor_tags/detail/cbor_encode_error.h"
#include "cbor_tags/detail/cwt_codec.h"
#include "cbor_tags/detail/cwt_fields.h"
#include "cbor_tags/detail/cwt_keys.h"

namespace cbor::tags::codec {

template <typename Self> struct cwt : tags::cwt::detail::key_codec<Self> {
    using tags::cwt::detail::key_codec<Self>::encode;
    using tags::cwt::detail::key_codec<Self>::decode;

    template <typename... Bindings> void encode(const tags::cwt::detail::field_map<Bindings...> &map) {
        tags::cwt::detail::encode_fields(static_cast<Self &>(*this), map);
    }

    template <typename... Bindings> status_code decode(tags::cwt::detail::field_map<Bindings...> &map, major_type major, std::byte info) {
        return tags::cwt::detail::decode_fields(static_cast<Self &>(*this), map, major, info);
    }

    template <typename... Bindings> status_code decode(tags::cwt::detail::field_map<Bindings...> &map) {
        auto      &dec = static_cast<Self &>(*this);
        major_type major{};
        std::byte  info{};
        const auto status = tags::detail::read_initial_byte(dec, major, info);
        return status == status_code::success ? decode(map, major, info) : status;
    }

    template <bool Borrowed, class Extra> void encode(const tags::cwt::basic_header_map<Borrowed, Extra> &object) {
        namespace helpers = tags::cwt::detail;
        if (helpers::validate_header_fields(object) != status_code::success) {
            throw tags::detail::encode_status_exception{status_code::error};
        }
        for (const auto &label : helpers::selected_header_labels(object)) {
            if (helpers::is_header_label(label, 7) || helpers::is_header_label(label, 9)) {
                throw tags::detail::encode_status_exception{status_code::error};
            }
        }

        std::optional<std::span<const typename decltype(object.crit)::value_type>> critical;
        if (!object.crit.empty()) {
            critical = object.crit;
        }
        auto map = helpers::header_fields(object, critical);
        helpers::encode_fields(static_cast<Self &>(*this), map);
    }

    template <bool Borrowed, class Extra> [[nodiscard]] status_code decode(tags::cwt::basic_header_map<Borrowed, Extra> &value) {
        auto      &dec = static_cast<Self &>(*this);
        major_type major{};
        std::byte  info{};
        const auto status = tags::detail::read_initial_byte(dec, major, info);
        return status == status_code::success ? decode(value, major, info) : status;
    }

    template <bool Borrowed, class Extra>
    [[nodiscard]] status_code decode(tags::cwt::basic_header_map<Borrowed, Extra> &destination, major_type major, std::byte info) {
        if constexpr (Borrowed && !IsContiguous<typename Self::input_buffer_type>) {
            return status_code::contiguous_view_on_non_contiguous_data;
        } else {
            namespace helpers                                = tags::cwt::detail;
            auto                                        &dec = static_cast<Self &>(*this);
            tags::cwt::basic_header_map<Borrowed, Extra> decoded{};
            std::optional<decltype(decoded.crit)>        critical;
            auto                                         map    = helpers::header_fields(decoded, critical);
            const auto                                   status = helpers::decode_fields(
                dec, map, major, info,
                [&](const tags::cwt::header_label &label) {
                    decoded.observed_labels.push_back(label);
                    return status_code::success;
                },
                [&](const tags::cwt::header_label &label, auto &value) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, tags::cwt::algorithm>) {
                        auto algorithm = helpers::decode_algorithm(dec);
                        if (!algorithm) {
                            return algorithm.error();
                        }
                        value = *algorithm;
                        return status_code::success;
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, decltype(decoded.crit)>) {
                        return helpers::decode_critical_labels(dec, value);
                    } else {
                        if (helpers::is_header_label(label, 15)) {
                            if (tags::detail::decoder_at_end(dec)) {
                                return status_code::incomplete;
                            }
                            // Inspect only the next item's major type, leaving ADL its complete item.
                            const auto initial = std::to_integer<std::uint8_t>(static_cast<std::byte>(*dec.tell()));
                            if ((initial >> 5) != static_cast<std::uint8_t>(major_type::Map)) {
                                return status_code::error;
                            }
                        }
                        return dec.decode(value);
                    }
                });
            if (status != status_code::success) {
                return status;
            }
            if (critical) {
                if (critical->empty()) {
                    return status_code::error;
                }
                decoded.crit = std::move(*critical);
            }
            if (helpers::validate_header_fields(decoded) != status_code::success) {
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
                    typename Self::encoded_item_view ignored;
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
                    helpers::audience_storage<Borrowed> value;
                    const auto                          value_status = dec.decode(value);
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
                    typename Self::encoded_item_view ignored;
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

    template <bool Borrowed, class Extra> void encode(const tags::cwt::basic_cose_signature<Borrowed, Extra> &object) {
        if (object.unprotected.typ) {
            throw tags::detail::encode_status_exception{status_code::error};
        }

        auto &enc                                              = static_cast<Self &>(*this);
        const auto &[protected_header, unprotected, signature] = object;

        enc.encode(as_array{3});
        enc.encode(protected_header);
        enc.encode(unprotected);
        enc.encode(signature);
    }

    template <bool Borrowed, class Extra> [[nodiscard]] status_code decode(tags::cwt::basic_cose_signature<Borrowed, Extra> &value) {
        auto      &dec = static_cast<Self &>(*this);
        major_type major{};
        std::byte  info{};
        const auto status = tags::detail::read_initial_byte(dec, major, info);
        return status == status_code::success ? decode(value, major, info) : status;
    }

    template <bool Borrowed, class Extra>
    [[nodiscard]] status_code decode(tags::cwt::basic_cose_signature<Borrowed, Extra> &destination, major_type major, std::byte info) {
        if constexpr (Borrowed && !IsContiguous<typename Self::input_buffer_type>) {
            return status_code::contiguous_view_on_non_contiguous_data;
        } else {
            namespace helpers                                    = tags::cwt::detail;
            auto                                            &dec = static_cast<Self &>(*this);
            tags::cwt::basic_cose_signature<Borrowed, Extra> decoded{};
            const auto                                       status =
                helpers::decode_array_fields(dec, major, info, decoded.protected_header, decoded.unprotected, decoded.signature);
            if (status != status_code::success) {
                return status;
            }
            if (decoded.unprotected.typ) {
                return status_code::error;
            }
            destination = std::move(decoded);
            return status_code::success;
        }
    }

    template <bool Borrowed, class BodyExtra, class SignerExtra>
    void encode(const tags::cwt::basic_cose_sign<Borrowed, BodyExtra, SignerExtra> &object) {
        if (object.unprotected.typ) {
            throw tags::detail::encode_status_exception{status_code::error};
        }

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

    template <bool Borrowed, class BodyExtra, class SignerExtra>
    [[nodiscard]] status_code decode(tags::cwt::basic_cose_sign<Borrowed, BodyExtra, SignerExtra> &value) {
        auto      &dec = static_cast<Self &>(*this);
        major_type major{};
        std::byte  info{};
        const auto status = tags::detail::read_initial_byte(dec, major, info);
        return status == status_code::success ? decode(value, major, info) : status;
    }

    template <bool Borrowed, class BodyExtra, class SignerExtra>
    [[nodiscard]] status_code decode(tags::cwt::basic_cose_sign<Borrowed, BodyExtra, SignerExtra> &destination, major_type major,
                                     std::byte info) {
        if constexpr (Borrowed && !IsContiguous<typename Self::input_buffer_type>) {
            return status_code::contiguous_view_on_non_contiguous_data;
        } else {
            namespace helpers                                                = tags::cwt::detail;
            auto                                                        &dec = static_cast<Self &>(*this);
            tags::cwt::basic_cose_sign<Borrowed, BodyExtra, SignerExtra> decoded{};
            const auto status = helpers::decode_array_fields(dec, major, info, decoded.protected_header, decoded.unprotected,
                                                             decoded.payload, decoded.signatures);
            if (status != status_code::success) {
                return status;
            }
            if (decoded.signatures.empty()) {
                return status_code::unexpected_group_size;
            }
            if (decoded.unprotected.typ) {
                return status_code::error;
            }
            destination = std::move(decoded);
            return status_code::success;
        }
    }

    template <bool Borrowed, class Extra> void encode(const tags::cwt::basic_cose_sign1<Borrowed, Extra> &object) {
        if (object.unprotected.typ) {
            throw tags::detail::encode_status_exception{status_code::error};
        }

        auto &enc                                                       = static_cast<Self &>(*this);
        const auto &[protected_header, unprotected, payload, signature] = object;

        enc.encode(as_array{4});
        enc.encode(protected_header);
        enc.encode(unprotected);
        enc.encode(payload);
        enc.encode(signature);
    }

    template <bool Borrowed, class Extra> [[nodiscard]] status_code decode(tags::cwt::basic_cose_sign1<Borrowed, Extra> &value) {
        auto      &dec = static_cast<Self &>(*this);
        major_type major{};
        std::byte  info{};
        const auto status = tags::detail::read_initial_byte(dec, major, info);
        return status == status_code::success ? decode(value, major, info) : status;
    }

    template <bool Borrowed, class Extra>
    [[nodiscard]] status_code decode(tags::cwt::basic_cose_sign1<Borrowed, Extra> &destination, major_type major, std::byte info) {
        if constexpr (Borrowed && !IsContiguous<typename Self::input_buffer_type>) {
            return status_code::contiguous_view_on_non_contiguous_data;
        } else {
            namespace helpers                                = tags::cwt::detail;
            auto                                        &dec = static_cast<Self &>(*this);
            tags::cwt::basic_cose_sign1<Borrowed, Extra> decoded{};
            const auto status = helpers::decode_array_fields(dec, major, info, decoded.protected_header, decoded.unprotected,
                                                             decoded.payload, decoded.signature);
            if (status != status_code::success) {
                return status;
            }
            if (decoded.unprotected.typ) {
                return status_code::error;
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
