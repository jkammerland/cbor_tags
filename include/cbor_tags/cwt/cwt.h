#pragma once

#include "cbor_tags/cbor_segments.h"
#include "cbor_tags/codec/cwt.h"

namespace cbor::tags::cwt {

template <typename T> [[nodiscard]] inline expected<byte_string, status_code> encode_to_bytes(T &&value) {
    byte_string output;
    auto        enc    = make_encoder<codec::cwt>(output);
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

template <typename Bytes> [[nodiscard]] inline expected<header_map, status_code> decode_protected_header(const Bytes &bytes) {
    header_map header{};
    if (bytes.empty()) {
        return header;
    }

    auto dec    = make_decoder<codec::cwt>(bytes);
    auto result = dec(header);
    if (!result) {
        return unexpected<status_code>{result.error()};
    }
    if (dec.tell() != bytes.end()) {
        return unexpected<status_code>{status_code::error};
    }
    return header;
}

[[nodiscard]] inline byte_string copy_bytes(std::span<const std::byte> bytes) { return byte_string{bytes.begin(), bytes.end()}; }

[[nodiscard]] inline expected<sig_structure, status_code>
make_sign1_sig_structure(const cose_sign1 &message, std::span<const std::byte> external_aad = {},
                         std::optional<std::span<const std::byte>> detached_payload = std::nullopt) {
    if (!message.payload && !detached_payload) {
        return unexpected<status_code>{status_code::error};
    }

    return sig_structure{
        .context        = "Signature1",
        .body_protected = message.protected_header,
        .sign_protected = std::nullopt,
        .external_aad   = copy_bytes(external_aad),
        .payload        = message.payload ? *message.payload : copy_bytes(*detached_payload),
    };
}

[[nodiscard]] inline expected<sig_structure, status_code>
make_sign_sig_structure(const cose_sign &message, const cose_signature &signature, std::span<const std::byte> external_aad = {},
                        std::optional<std::span<const std::byte>> detached_payload = std::nullopt) {
    if (!message.payload && !detached_payload) {
        return unexpected<status_code>{status_code::error};
    }

    return sig_structure{
        .context        = "Signature",
        .body_protected = message.protected_header,
        .sign_protected = signature.protected_header,
        .external_aad   = copy_bytes(external_aad),
        .payload        = message.payload ? *message.payload : copy_bytes(*detached_payload),
    };
}

template <bool Borrowed>
[[nodiscard]] inline expected<byte_string, status_code>
make_sign1_tbs(const basic_cose_sign1<Borrowed> &message, std::span<const std::byte> external_aad = {},
               std::optional<std::span<const std::byte>> detached_payload = std::nullopt) {
    if (!message.payload && !detached_payload) {
        return unexpected<status_code>{status_code::error};
    }
    const sig_structure_view structure{.context        = "Signature1",
                                       .body_protected = message.protected_header,
                                       .sign_protected = std::nullopt,
                                       .external_aad   = external_aad,
                                       .payload        = message.payload ? byte_view{*message.payload} : *detached_payload};
    return encode_to_bytes(structure);
}

template <bool Borrowed, bool SignatureBorrowed>
[[nodiscard]] inline expected<byte_string, status_code>
make_sign_tbs(const basic_cose_sign<Borrowed> &message, const basic_cose_signature<SignatureBorrowed> &signature,
              std::span<const std::byte> external_aad = {}, std::optional<std::span<const std::byte>> detached_payload = std::nullopt) {
    if (!message.payload && !detached_payload) {
        return unexpected<status_code>{status_code::error};
    }
    const sig_structure_view structure{.context        = "Signature",
                                       .body_protected = message.protected_header,
                                       .sign_protected = byte_view{signature.protected_header},
                                       .external_aad   = external_aad,
                                       .payload        = message.payload ? byte_view{*message.payload} : *detached_payload};
    return encode_to_bytes(structure);
}

template <bool Borrowed>
[[nodiscard]] inline expected<void, status_code>
validate_sign1_algorithm(const header_map &protected_header, const basic_header_map<Borrowed> &unprotected, algorithm expected) {
    if (unprotected.alg || !unprotected.crit.empty()) {
        return unexpected<status_code>{status_code::error};
    }
    if (protected_header.alg && *protected_header.alg != expected) {
        return unexpected<status_code>{status_code::error};
    }
    return {};
}

template <bool BodyBorrowed, bool SignatureBorrowed>
[[nodiscard]] inline expected<void, status_code>
validate_sign_algorithm(const header_map &body_protected, const basic_header_map<BodyBorrowed> &body_unprotected,
                        const header_map &signature_protected, const basic_header_map<SignatureBorrowed> &signature_unprotected,
                        algorithm expected) {
    if (body_unprotected.alg || !body_unprotected.crit.empty() || signature_unprotected.alg || !signature_unprotected.crit.empty()) {
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
               std::span<const std::byte> external_aad = {}, std::optional<std::span<const std::byte>> detached_payload = std::nullopt) {
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
                                                               std::optional<std::span<const std::byte>> detached_payload = std::nullopt) {
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

template <typename Backend, typename VerificationKey, bool Borrowed>
[[nodiscard]] inline expected<void, status_code> verify_sign1(VerificationKey &&key, const basic_cose_sign1<Borrowed> &message,
                                                              std::span<const std::byte>                external_aad     = {},
                                                              std::optional<std::span<const std::byte>> detached_payload = std::nullopt) {
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

template <typename Backend, typename VerificationKey, bool Borrowed, bool SignatureBorrowed>
[[nodiscard]] inline expected<void, status_code>
verify_signature(VerificationKey &&key, const basic_cose_sign<Borrowed> &message, const basic_cose_signature<SignatureBorrowed> &signature,
                 std::span<const std::byte> external_aad = {}, std::optional<std::span<const std::byte>> detached_payload = std::nullopt) {
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

template <typename Backend, typename VerificationKey, bool Borrowed>
[[nodiscard]] inline expected<void, status_code> verify_sign(VerificationKey &&key, const basic_cose_sign<Borrowed> &message,
                                                             std::size_t signature_index, std::span<const std::byte> external_aad = {},
                                                             std::optional<std::span<const std::byte>> detached_payload = std::nullopt) {
    if (signature_index >= message.signatures.size()) {
        return unexpected<status_code>{status_code::unexpected_group_size};
    }
    return verify_signature<Backend>(std::forward<VerificationKey>(key), message, message.signatures[signature_index], external_aad,
                                     detached_payload);
}

template <typename Backend, typename VerificationKey, bool Borrowed>
[[nodiscard]] inline expected<void, status_code> verify_sign(VerificationKey &&key, const basic_cose_sign<Borrowed> &message,
                                                             std::span<const std::byte>                external_aad     = {},
                                                             std::optional<std::span<const std::byte>> detached_payload = std::nullopt) {
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

template <bool Borrowed> [[nodiscard]] inline auto as_cose_sign1(basic_cose_sign1<Borrowed> value) {
    return make_tag_pair(cose_sign1_tag{}, std::move(value));
}
template <bool Borrowed> [[nodiscard]] inline auto as_cose_sign(basic_cose_sign<Borrowed> value) {
    return make_tag_pair(cose_sign_tag{}, std::move(value));
}

template <typename T> [[nodiscard]] inline auto as_cwt(T value) { return make_tag_pair(cwt_tag{}, std::move(value)); }

} // namespace cbor::tags::cwt
