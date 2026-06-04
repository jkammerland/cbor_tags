#pragma once

#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_integer.h"
#include "cbor_tags/cbor_segments.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cbor::tags::cwt {

using byte_string  = std::vector<std::byte>;
using numeric_date = std::variant<std::int64_t, double>;

inline constexpr std::uint64_t cwt_tag_value        = 61;
inline constexpr std::uint64_t cose_sign1_tag_value = 18;
inline constexpr std::uint64_t cose_sign_tag_value  = 98;

using cwt_tag        = static_tag<cwt_tag_value>;
using cose_sign1_tag = static_tag<cose_sign1_tag_value>;
using cose_sign_tag  = static_tag<cose_sign_tag_value>;

enum class algorithm : std::int64_t {
    es256 = -7,
    es384 = -35,
    es512 = -36,
};

struct header_map {
    std::optional<algorithm>   alg;
    std::optional<byte_string> kid;

    [[nodiscard]] constexpr bool empty() const noexcept { return !alg && !kid; }

    template <typename Encoder> constexpr auto encode(Encoder &enc) const {
        std::uint64_t size{};
        if (alg) {
            ++size;
        }
        if (kid) {
            ++size;
        }

        auto result = enc(as_map{size});
        if (!result) {
            return result;
        }
        if (alg) {
            result = enc(std::uint64_t{1}, *alg);
            if (!result) {
                return result;
            }
        }
        if (kid) {
            result = enc(std::uint64_t{4}, *kid);
        }
        return result;
    }

    template <typename Decoder> constexpr auto decode(Decoder &dec) {
        as_map_any map{};
        auto       result = dec(map);
        if (!result) {
            return result;
        }

        header_map decoded{};
        for (std::uint64_t index = 0; index < map.size; ++index) {
            integer key{0};
            result = dec(key);
            if (!result) {
                return result;
            }

            if (!key.is_negative && key.value == 1U) {
                algorithm value{};
                result = dec(value);
                if (!result) {
                    return result;
                }
                decoded.alg = value;
            } else if (!key.is_negative && key.value == 4U) {
                byte_string value;
                result = dec(value);
                if (!result) {
                    return result;
                }
                decoded.kid = std::move(value);
            } else {
                typename Decoder::raw_encoded_item_view ignored;
                result = dec(ignored);
                if (!result) {
                    return result;
                }
            }
        }

        *this = std::move(decoded);
        return result;
    }
};

struct claims_set {
    std::optional<std::string>  issuer;
    std::optional<std::string>  subject;
    std::optional<std::string>  audience;
    std::optional<numeric_date> expiration;
    std::optional<numeric_date> not_before;
    std::optional<numeric_date> issued_at;
    std::optional<byte_string>  cwt_id;

    template <typename Encoder> constexpr auto encode(Encoder &enc) const {
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

        auto result = enc(as_map{size});
        if (!result) {
            return result;
        }
        if (issuer) {
            result = enc(std::uint64_t{1}, *issuer);
            if (!result) {
                return result;
            }
        }
        if (subject) {
            result = enc(std::uint64_t{2}, *subject);
            if (!result) {
                return result;
            }
        }
        if (audience) {
            result = enc(std::uint64_t{3}, *audience);
            if (!result) {
                return result;
            }
        }
        if (expiration) {
            result = enc(std::uint64_t{4}, *expiration);
            if (!result) {
                return result;
            }
        }
        if (not_before) {
            result = enc(std::uint64_t{5}, *not_before);
            if (!result) {
                return result;
            }
        }
        if (issued_at) {
            result = enc(std::uint64_t{6}, *issued_at);
            if (!result) {
                return result;
            }
        }
        if (cwt_id) {
            result = enc(std::uint64_t{7}, *cwt_id);
        }
        return result;
    }

    template <typename Decoder> constexpr auto decode(Decoder &dec) {
        as_map_any map{};
        auto       result = dec(map);
        if (!result) {
            return result;
        }

        claims_set decoded{};
        for (std::uint64_t index = 0; index < map.size; ++index) {
            integer key{0};
            result = dec(key);
            if (!result) {
                return result;
            }

            if (key.is_negative) {
                typename Decoder::raw_encoded_item_view ignored;
                result = dec(ignored);
            } else if (key.value == 1U) {
                std::string value;
                result         = dec(value);
                decoded.issuer = std::move(value);
            } else if (key.value == 2U) {
                std::string value;
                result          = dec(value);
                decoded.subject = std::move(value);
            } else if (key.value == 3U) {
                std::string value;
                result           = dec(value);
                decoded.audience = std::move(value);
            } else if (key.value == 4U) {
                numeric_date value{};
                result             = dec(value);
                decoded.expiration = std::move(value);
            } else if (key.value == 5U) {
                numeric_date value{};
                result             = dec(value);
                decoded.not_before = std::move(value);
            } else if (key.value == 6U) {
                numeric_date value{};
                result            = dec(value);
                decoded.issued_at = std::move(value);
            } else if (key.value == 7U) {
                byte_string value;
                result         = dec(value);
                decoded.cwt_id = std::move(value);
            } else {
                typename Decoder::raw_encoded_item_view ignored;
                result = dec(ignored);
            }

            if (!result) {
                return result;
            }
        }

        *this = std::move(decoded);
        return result;
    }
};

struct cose_signature {
    byte_string protected_header;
    header_map  unprotected;
    byte_string signature;

    template <typename Encoder> constexpr auto encode(Encoder &enc) const {
        return enc(as_array{3}, protected_header, unprotected, signature);
    }

    template <typename Decoder> constexpr auto decode(Decoder &dec) { return dec(as_array{3}, protected_header, unprotected, signature); }
};

struct cose_sign {
    byte_string                 protected_header;
    header_map                  unprotected;
    std::optional<byte_string>  payload;
    std::vector<cose_signature> signatures;

    template <typename Encoder> constexpr auto encode(Encoder &enc) const {
        return enc(as_array{4}, protected_header, unprotected, payload, signatures);
    }

    template <typename Decoder> constexpr auto decode(Decoder &dec) {
        return dec(as_array{4}, protected_header, unprotected, payload, signatures);
    }
};

struct cose_sign1 {
    byte_string                protected_header;
    header_map                 unprotected;
    std::optional<byte_string> payload;
    byte_string                signature;

    template <typename Encoder> constexpr auto encode(Encoder &enc) const {
        return enc(as_array{4}, protected_header, unprotected, payload, signature);
    }

    template <typename Decoder> constexpr auto decode(Decoder &dec) {
        return dec(as_array{4}, protected_header, unprotected, payload, signature);
    }
};

struct sig_structure {
    std::string                context;
    byte_string                body_protected;
    std::optional<byte_string> sign_protected;
    byte_string                external_aad;
    byte_string                payload;

    template <typename Encoder> constexpr auto encode(Encoder &enc) const {
        if (sign_protected) {
            return enc(as_array{5}, context, body_protected, *sign_protected, external_aad, payload);
        }
        return enc(as_array{4}, context, body_protected, external_aad, payload);
    }

    template <typename Decoder> constexpr auto decode(Decoder &dec) {
        as_array_any array{};
        auto         result = dec(array);
        if (!result) {
            return result;
        }
        if (array.size == 4U) {
            sign_protected.reset();
            return dec(context, body_protected, external_aad, payload);
        }
        if (array.size == 5U) {
            byte_string decoded_sign_protected;
            result = dec(context, body_protected, decoded_sign_protected, external_aad, payload);
            if (result) {
                sign_protected = std::move(decoded_sign_protected);
            }
            return result;
        }
        return typename Decoder::expected_type{unexpected<status_code>{status_code::unexpected_group_size}};
    }
};

template <typename T> [[nodiscard]] inline expected<byte_string, status_code> encode_to_bytes(T &&value) {
    byte_string output;
    auto        enc    = make_encoder(output);
    auto        result = enc(std::forward<T>(value));
    if (!result) {
        return unexpected<status_code>{result.error()};
    }
    return output;
}

[[nodiscard]] inline expected<byte_string, status_code> encode_protected_header(const header_map &header) {
    if (header.empty()) {
        return byte_string{};
    }
    return encode_to_bytes(header);
}

[[nodiscard]] inline expected<header_map, status_code> decode_protected_header(const byte_string &bytes) {
    header_map header{};
    if (bytes.empty()) {
        return header;
    }

    auto dec    = make_decoder(bytes);
    auto result = dec(header);
    if (!result) {
        return unexpected<status_code>{result.error()};
    }
    return header;
}

[[nodiscard]] inline byte_string copy_bytes(std::span<const std::byte> bytes) { return byte_string{bytes.begin(), bytes.end()}; }

[[nodiscard]] inline expected<sig_structure, status_code> make_sign1_sig_structure(const cose_sign1          &message,
                                                                                   std::span<const std::byte> external_aad     = {},
                                                                                   std::span<const std::byte> detached_payload = {}) {
    if (!message.payload && detached_payload.empty()) {
        return unexpected<status_code>{status_code::error};
    }

    return sig_structure{
        .context        = "Signature1",
        .body_protected = message.protected_header,
        .sign_protected = std::nullopt,
        .external_aad   = copy_bytes(external_aad),
        .payload        = message.payload ? *message.payload : copy_bytes(detached_payload),
    };
}

[[nodiscard]] inline expected<sig_structure, status_code> make_sign_sig_structure(const cose_sign &message, const cose_signature &signature,
                                                                                  std::span<const std::byte> external_aad     = {},
                                                                                  std::span<const std::byte> detached_payload = {}) {
    if (!message.payload && detached_payload.empty()) {
        return unexpected<status_code>{status_code::error};
    }

    return sig_structure{
        .context        = "Signature",
        .body_protected = message.protected_header,
        .sign_protected = signature.protected_header,
        .external_aad   = copy_bytes(external_aad),
        .payload        = message.payload ? *message.payload : copy_bytes(detached_payload),
    };
}

[[nodiscard]] inline expected<byte_string, status_code>
make_sign1_tbs(const cose_sign1 &message, std::span<const std::byte> external_aad = {}, std::span<const std::byte> detached_payload = {}) {
    auto structure = make_sign1_sig_structure(message, external_aad, detached_payload);
    if (!structure) {
        return unexpected<status_code>{structure.error()};
    }
    return encode_to_bytes(*structure);
}

[[nodiscard]] inline expected<byte_string, status_code> make_sign_tbs(const cose_sign &message, const cose_signature &signature,
                                                                      std::span<const std::byte> external_aad     = {},
                                                                      std::span<const std::byte> detached_payload = {}) {
    auto structure = make_sign_sig_structure(message, signature, external_aad, detached_payload);
    if (!structure) {
        return unexpected<status_code>{structure.error()};
    }
    return encode_to_bytes(*structure);
}

[[nodiscard]] inline expected<void, status_code> validate_sign1_algorithm(const header_map &protected_header, const header_map &unprotected,
                                                                          algorithm expected) {
    if (unprotected.alg) {
        return unexpected<status_code>{status_code::error};
    }
    if (protected_header.alg && *protected_header.alg != expected) {
        return unexpected<status_code>{status_code::error};
    }
    return {};
}

[[nodiscard]] inline expected<void, status_code> validate_sign_algorithm(const header_map &body_protected,
                                                                         const header_map &body_unprotected,
                                                                         const header_map &signature_protected,
                                                                         const header_map &signature_unprotected, algorithm expected) {
    if (body_unprotected.alg || signature_unprotected.alg) {
        return unexpected<status_code>{status_code::error};
    }
    if (body_protected.alg && *body_protected.alg != expected) {
        return unexpected<status_code>{status_code::error};
    }
    if (signature_protected.alg && *signature_protected.alg != expected) {
        return unexpected<status_code>{status_code::error};
    }
    return {};
}

template <typename Backend, typename SigningKey>
[[nodiscard]] inline expected<cose_sign1, status_code> sign1(SigningKey &&key, header_map protected_header, header_map unprotected,
                                                             std::span<const std::byte> payload,
                                                             std::span<const std::byte> external_aad = {}) {
    auto algorithm_status = validate_sign1_algorithm(protected_header, unprotected, Backend::algorithm_id);
    if (!algorithm_status) {
        return unexpected<status_code>{algorithm_status.error()};
    }
    if (!protected_header.alg) {
        protected_header.alg = Backend::algorithm_id;
    }

    auto protected_bytes = encode_protected_header(protected_header);
    if (!protected_bytes) {
        return unexpected<status_code>{protected_bytes.error()};
    }

    cose_sign1 message{
        .protected_header = std::move(*protected_bytes),
        .unprotected      = std::move(unprotected),
        .payload          = copy_bytes(payload),
        .signature        = {},
    };

    auto to_be_signed = make_sign1_tbs(message, external_aad);
    if (!to_be_signed) {
        return unexpected<status_code>{to_be_signed.error()};
    }

    auto signature = Backend::sign(std::forward<SigningKey>(key), std::span<const std::byte>{to_be_signed->data(), to_be_signed->size()});
    if (!signature) {
        return unexpected<status_code>{signature.error()};
    }
    message.signature = std::move(*signature);
    return message;
}

template <typename Backend, typename SigningKey>
[[nodiscard]] inline expected<cose_signature, status_code>
sign_signature(SigningKey &&key, const cose_sign &message, header_map protected_header, header_map unprotected,
               std::span<const std::byte> external_aad = {}, std::span<const std::byte> detached_payload = {}) {
    auto body_protected = decode_protected_header(message.protected_header);
    if (!body_protected) {
        return unexpected<status_code>{body_protected.error()};
    }

    auto algorithm_status =
        validate_sign_algorithm(*body_protected, message.unprotected, protected_header, unprotected, Backend::algorithm_id);
    if (!algorithm_status) {
        return unexpected<status_code>{algorithm_status.error()};
    }
    if (!body_protected->alg && !protected_header.alg) {
        protected_header.alg = Backend::algorithm_id;
    }

    auto protected_bytes = encode_protected_header(protected_header);
    if (!protected_bytes) {
        return unexpected<status_code>{protected_bytes.error()};
    }

    cose_signature signature{
        .protected_header = std::move(*protected_bytes),
        .unprotected      = std::move(unprotected),
        .signature        = {},
    };

    auto to_be_signed = make_sign_tbs(message, signature, external_aad, detached_payload);
    if (!to_be_signed) {
        return unexpected<status_code>{to_be_signed.error()};
    }

    auto signature_bytes =
        Backend::sign(std::forward<SigningKey>(key), std::span<const std::byte>{to_be_signed->data(), to_be_signed->size()});
    if (!signature_bytes) {
        return unexpected<status_code>{signature_bytes.error()};
    }
    signature.signature = std::move(*signature_bytes);
    return signature;
}

template <typename Backend, typename SigningKey>
[[nodiscard]] inline expected<void, status_code> add_signature(SigningKey &&key, cose_sign &message, header_map protected_header,
                                                               header_map unprotected, std::span<const std::byte> external_aad = {},
                                                               std::span<const std::byte> detached_payload = {}) {
    auto signature = sign_signature<Backend>(std::forward<SigningKey>(key), message, std::move(protected_header), std::move(unprotected),
                                             external_aad, detached_payload);
    if (!signature) {
        return unexpected<status_code>{signature.error()};
    }
    message.signatures.push_back(std::move(*signature));
    return {};
}

template <typename Backend, typename SigningKey>
[[nodiscard]] inline expected<cose_sign, status_code>
sign(SigningKey &&key, header_map protected_header, header_map unprotected, std::span<const std::byte> payload,
     header_map signature_protected_header = {}, header_map signature_unprotected = {}, std::span<const std::byte> external_aad = {}) {
    auto algorithm_status =
        validate_sign_algorithm(protected_header, unprotected, signature_protected_header, signature_unprotected, Backend::algorithm_id);
    if (!algorithm_status) {
        return unexpected<status_code>{algorithm_status.error()};
    }

    auto protected_bytes = encode_protected_header(protected_header);
    if (!protected_bytes) {
        return unexpected<status_code>{protected_bytes.error()};
    }

    cose_sign message{
        .protected_header = std::move(*protected_bytes),
        .unprotected      = std::move(unprotected),
        .payload          = copy_bytes(payload),
        .signatures       = {},
    };

    auto signature = sign_signature<Backend>(std::forward<SigningKey>(key), message, std::move(signature_protected_header),
                                             std::move(signature_unprotected), external_aad);
    if (!signature) {
        return unexpected<status_code>{signature.error()};
    }
    message.signatures.push_back(std::move(*signature));
    return message;
}

template <typename Backend, typename VerificationKey>
[[nodiscard]] inline expected<void, status_code> verify_sign1(VerificationKey &&key, const cose_sign1 &message,
                                                              std::span<const std::byte> external_aad     = {},
                                                              std::span<const std::byte> detached_payload = {}) {
    auto protected_header = decode_protected_header(message.protected_header);
    if (!protected_header) {
        return unexpected<status_code>{protected_header.error()};
    }
    auto algorithm_status = validate_sign1_algorithm(*protected_header, message.unprotected, Backend::algorithm_id);
    if (!algorithm_status) {
        return unexpected<status_code>{algorithm_status.error()};
    }

    auto to_be_signed = make_sign1_tbs(message, external_aad, detached_payload);
    if (!to_be_signed) {
        return unexpected<status_code>{to_be_signed.error()};
    }

    return Backend::verify(std::forward<VerificationKey>(key), std::span<const std::byte>{to_be_signed->data(), to_be_signed->size()},
                           std::span<const std::byte>{message.signature.data(), message.signature.size()});
}

template <typename Backend, typename VerificationKey>
[[nodiscard]] inline expected<void, status_code>
verify_signature(VerificationKey &&key, const cose_sign &message, const cose_signature &signature,
                 std::span<const std::byte> external_aad = {}, std::span<const std::byte> detached_payload = {}) {
    auto body_protected = decode_protected_header(message.protected_header);
    if (!body_protected) {
        return unexpected<status_code>{body_protected.error()};
    }
    auto signature_protected = decode_protected_header(signature.protected_header);
    if (!signature_protected) {
        return unexpected<status_code>{signature_protected.error()};
    }
    auto algorithm_status =
        validate_sign_algorithm(*body_protected, message.unprotected, *signature_protected, signature.unprotected, Backend::algorithm_id);
    if (!algorithm_status) {
        return unexpected<status_code>{algorithm_status.error()};
    }

    auto to_be_signed = make_sign_tbs(message, signature, external_aad, detached_payload);
    if (!to_be_signed) {
        return unexpected<status_code>{to_be_signed.error()};
    }

    return Backend::verify(std::forward<VerificationKey>(key), std::span<const std::byte>{to_be_signed->data(), to_be_signed->size()},
                           std::span<const std::byte>{signature.signature.data(), signature.signature.size()});
}

template <typename Backend, typename VerificationKey>
[[nodiscard]] inline expected<void, status_code> verify_sign(VerificationKey &&key, const cose_sign &message, std::size_t signature_index,
                                                             std::span<const std::byte> external_aad     = {},
                                                             std::span<const std::byte> detached_payload = {}) {
    if (signature_index >= message.signatures.size()) {
        return unexpected<status_code>{status_code::unexpected_group_size};
    }
    return verify_signature<Backend>(std::forward<VerificationKey>(key), message, message.signatures[signature_index], external_aad,
                                     detached_payload);
}

template <typename Backend, typename VerificationKey>
[[nodiscard]] inline expected<void, status_code> verify_sign(VerificationKey &&key, const cose_sign &message,
                                                             std::span<const std::byte> external_aad     = {},
                                                             std::span<const std::byte> detached_payload = {}) {
    if (message.signatures.empty()) {
        return unexpected<status_code>{status_code::unexpected_group_size};
    }
    for (const auto &signature : message.signatures) {
        auto result = verify_signature<Backend>(key, message, signature, external_aad, detached_payload);
        if (!result) {
            return result;
        }
    }
    return {};
}

[[nodiscard]] inline auto as_cose_sign1(cose_sign1 value) { return make_tag_pair(cose_sign1_tag{}, std::move(value)); }
[[nodiscard]] inline auto as_cose_sign(cose_sign value) { return make_tag_pair(cose_sign_tag{}, std::move(value)); }

template <typename T> [[nodiscard]] inline auto as_cwt(T value) { return make_tag_pair(cwt_tag{}, std::move(value)); }

} // namespace cbor::tags::cwt
