#pragma once

#include "cbor_tags/cwt/confirmation.h"
#include "cbor_tags/detail/cbor_encode_error.h"
#include "cbor_tags/detail/cwt_codec.h"

namespace cbor::tags::cwt::detail {

template <typename Self> struct key_codec : codec::base<Self> {
    using codec::base<Self>::encode;
    using codec::base<Self>::decode;

    template <bool B> void        encode(const basic_ec2_public_key<B> &key) { encode_key(key); }
    template <bool B> status_code decode(basic_ec2_public_key<B> &key) { return decode_key(key); }
    template <bool B> status_code decode(basic_ec2_public_key<B> &key, major_type major, std::byte info) {
        return decode_key(key, major, info);
    }
    template <bool B> void        encode(const basic_okp_public_key<B> &key) { encode_key(key); }
    template <bool B> status_code decode(basic_okp_public_key<B> &key) { return decode_key(key); }
    template <bool B> status_code decode(basic_okp_public_key<B> &key, major_type major, std::byte info) {
        return decode_key(key, major, info);
    }
    template <bool B> void        encode(const basic_rsa_public_key<B> &key) { encode_key(key); }
    template <bool B> status_code decode(basic_rsa_public_key<B> &key) { return decode_key(key); }
    template <bool B> status_code decode(basic_rsa_public_key<B> &key, major_type major, std::byte info) {
        return decode_key(key, major, info);
    }

    template <PublicKey Key> void encode_key(const Key &key) {
        if (!valid_public_key_structure(key))
            throw tags::detail::encode_status_exception{status_code::error};
        auto         &enc   = static_cast<Self &>(*this);
        std::uint64_t count = Key::key_type == 2 ? 4 : 3;
        count += key.kid.has_value() + key.alg.has_value() + key.key_ops.has_value();
        enc.encode(as_map{count});
        enc.encode(1);
        enc.encode(Key::key_type);
        if (key.kid) {
            enc.encode(2);
            enc.encode(*key.kid);
        }
        if (key.alg) {
            enc.encode(3);
            enc.encode(*key.alg);
        }
        if (key.key_ops) {
            enc.encode(4);
            enc.encode(*key.key_ops);
        }
        if constexpr (Key::key_type == 3) {
            enc.encode(-1);
            enc.encode(key.modulus);
            enc.encode(-2);
            enc.encode(key.exponent);
        } else {
            enc.encode(-1);
            enc.encode(key.curve);
            enc.encode(-2);
            enc.encode(key.x);
            if constexpr (Key::key_type == 2) {
                enc.encode(-3);
                enc.encode(key.y);
            }
        }
    }

    template <PublicKey Key> status_code decode_key(Key &key) {
        auto      &dec = static_cast<Self &>(*this);
        major_type major{};
        std::byte  info{};
        auto       status = tags::detail::read_initial_byte(dec, major, info);
        return status == status_code::success ? decode_key(key, major, info) : status;
    }

    template <PublicKey Key> status_code decode_key(Key &destination, major_type major, std::byte info) {
        if constexpr (Key::borrowed && !IsContiguous<typename Self::input_buffer_type>) {
            return status_code::contiguous_view_on_non_contiguous_data;
        } else {
            auto                &dec = static_cast<Self &>(*this);
            Key                  key;
            bool                 type_seen = false, first = false, second = false, third = false;
            seen_header_labels<> seen;
            auto                 status = decode_map_entries(dec, major, info, [&](major_type km, std::byte ki) {
                auto label = decode_header_label(dec, km, ki);
                if (!label)
                    return label.error();
                seen.add(*label);
                auto *number = std::get_if<integer>(&*label);
                if (number && number->value <= 100) {
                    auto n = static_cast<std::int64_t>(number->value) * (number->is_negative ? -1 : 1);
                    switch (n) {
                    case 1: {
                        std::int64_t type{};
                        auto         result = dec.decode(type);
                        if (result != status_code::success)
                            return result;
                        type_seen = type == Key::key_type;
                        return type_seen ? status_code::success : status_code::error;
                    }
                    case 2: {
                        byte_storage<Key::borrowed> value;
                        auto                        s = dec.decode(value);
                        if (s == status_code::success)
                            key.kid = std::move(value);
                        return s;
                    }
                    case 3: {
                        auto value = decode_algorithm(dec);
                        if (!value)
                            return value.error();
                        key.alg = *value;
                        return status_code::success;
                    }
                    case 4: {
                        std::vector<std::int64_t> value;
                        auto                      s = dec.decode(value);
                        if (s == status_code::success)
                            key.key_ops = std::move(value);
                        return s;
                    }
                    case -1:
                        first = true;
                        if constexpr (Key::key_type == 3)
                            return dec.decode(key.modulus);
                        else
                            return dec.decode(key.curve);
                    case -2:
                        second = true;
                        if constexpr (Key::key_type == 3)
                            return dec.decode(key.exponent);
                        else
                            return dec.decode(key.x);
                    case -3:
                        if constexpr (Key::key_type == 2) {
                            third = true;
                            return dec.decode(key.y);
                        } else if constexpr (Key::key_type == 3)
                            return status_code::error;
                        break;
                    default:
                        if (n == -4 || (Key::key_type == 3 && n <= -3 && n >= -12))
                            return status_code::error;
                    }
                }
                typename Self::encoded_item_view ignored;
                return dec.decode(ignored);
            });
            if (status != status_code::success)
                return status;
            if (!type_seen || !first || !second || (Key::key_type == 2 && !third) || seen.has_duplicates() ||
                !valid_public_key_structure(key))
                return status_code::error;
            destination = std::move(key);
            return status_code::success;
        }
    }

    template <typename Key, bool B> void encode(const basic_confirmation<Key, B> &value) {
        auto &enc = static_cast<Self &>(*this);
        if (value.thumbprint && value.thumbprint->size() != 32)
            throw tags::detail::encode_status_exception{status_code::error};
        enc.encode(as_map{static_cast<std::uint64_t>(value.key.has_value() + value.kid.has_value() + value.thumbprint.has_value())});
        if (value.key) {
            enc.encode(1);
            enc.encode(*value.key);
        }
        if (value.kid) {
            enc.encode(3);
            enc.encode(*value.kid);
        }
        if (value.thumbprint) {
            enc.encode(5);
            enc.encode(*value.thumbprint);
        }
    }

    template <typename Key, bool B> status_code decode(basic_confirmation<Key, B> &value) {
        auto      &dec = static_cast<Self &>(*this);
        major_type major{};
        std::byte  info{};
        auto       status = tags::detail::read_initial_byte(dec, major, info);
        return status == status_code::success ? decode(value, major, info) : status;
    }

    template <typename Key, bool B> status_code decode(basic_confirmation<Key, B> &destination, major_type major, std::byte info) {
        auto                      &dec = static_cast<Self &>(*this);
        basic_confirmation<Key, B> value;
        bool                       encrypted = false;
        seen_header_labels<>       seen;
        auto                       status = decode_map_entries(dec, major, info, [&](major_type km, std::byte ki) {
            auto label = decode_header_label(dec, km, ki);
            if (!label)
                return label.error();
            seen.add(*label);
            if (is_header_label(*label, 1)) {
                Key  key;
                auto s = dec.decode(key);
                if (s == status_code::success)
                    value.key = std::move(key);
                return s;
            }
            if (is_header_label(*label, 3) || is_header_label(*label, 5)) {
                byte_storage<B> bytes;
                auto            s = dec.decode(bytes);
                if (s == status_code::success) {
                    if (is_header_label(*label, 3))
                        value.kid = std::move(bytes);
                    else
                        value.thumbprint = std::move(bytes);
                }
                return s;
            }
            if (is_header_label(*label, 2))
                encrypted = true;
            typename Self::encoded_item_view ignored;
            return dec.decode(ignored);
        });
        if (status != status_code::success)
            return status;
        if (seen.has_duplicates() || (encrypted && value.key) || (value.thumbprint && value.thumbprint->size() != 32))
            return status_code::error;
        destination = std::move(value);
        return status_code::success;
    }
};
} // namespace cbor::tags::cwt::detail
