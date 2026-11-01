#pragma once

#include "cbor_tags/cbor_segments.h"
#include "cbor_tags/codec/cwt.h"
#include "cbor_tags/cwt/detail/signature_input.h"

namespace cbor::tags::cwt {

// The application must authenticate the algorithm choice outside the COSE message.
struct implicit_algorithm_profile_t {
    // No default constructor: existing calls may use {} for an empty AAD span.
    explicit constexpr implicit_algorithm_profile_t(std::in_place_t) {}
};
inline constexpr implicit_algorithm_profile_t implicit_algorithm_profile{std::in_place};

namespace detail {
enum class verification_algorithm_policy { explicit_algorithm, implicit_algorithm };

inline expected<void, status_code> validate_verification_algorithm(const std::optional<algorithm> &selected, algorithm backend,
                                                                   verification_algorithm_policy policy) {
    const bool accepted = policy == verification_algorithm_policy::implicit_algorithm ? !selected : selected && *selected == backend;
    if (!accepted) {
        return unexpected<status_code>{status_code::error};
    }
    return {};
}
} // namespace detail

template <template <typename> typename... Codecs, typename T>
[[nodiscard]] inline expected<std::vector<std::byte>, status_code> encode_to_bytes(T &&value) {
    std::vector<std::byte> output;
    auto                   enc    = make_encoder<codec::cwt, Codecs...>(output);
    auto                   result = enc(std::forward<T>(value));
    if (!result) {
        return unexpected<status_code>{result.error()};
    }
    return output;
}

template <template <typename> typename... Codecs, bool Borrowed, class Extra>
[[nodiscard]] inline expected<std::vector<std::byte>, status_code>
encode_protected_header(const basic_header_map<Borrowed, Extra> &header) {
    if (detail::validate_protected_placement(header) != status_code::success) {
        return unexpected<status_code>{status_code::error};
    }
    if (header.empty()) {
        return std::vector<std::byte>{};
    }
    return encode_to_bytes<Codecs...>(header);
}

// A borrowed destination keeps references into the caller's named input buffer.
template <template <typename> typename... Codecs, typename Bytes, bool Borrowed, class Extra>
[[nodiscard]] inline expected<void, status_code> decode_protected_header(Bytes &bytes, basic_header_map<Borrowed, Extra> &destination) {
    basic_header_map<Borrowed, Extra> decoded{};
    if (bytes.empty()) {
        // An empty protected bstr has the same field requirements as an empty map.
        auto extra    = detail::extra_fields(decoded);
        bool required = false;
        detail::for_each_field(extra, [&](const auto &binding) {
            if constexpr (detail::field_optional<std::remove_cvref_t<decltype(*binding.value)>>) {
                binding.value->reset();
            } else {
                required = true;
            }
        });
        if (required) {
            return unexpected<status_code>{status_code::error};
        }
        destination = std::move(decoded);
        return {};
    }

    auto decoder = make_decoder<codec::cwt, Codecs...>(bytes);
    auto result  = decoder(decoded);
    if (!result) {
        return unexpected<status_code>{result.error()};
    }
    if (decoder.tell() != bytes.end() || detail::validate_protected_placement(decoded) != status_code::success) {
        return unexpected<status_code>{status_code::error};
    }
    destination = std::move(decoded);
    return {};
}

template <typename Bytes> [[nodiscard]] inline expected<header_map, status_code> decode_protected_header(const Bytes &bytes) {
    header_map header{};
    auto       result = decode_protected_header(bytes, header);
    if (!result) {
        return unexpected<status_code>{result.error()};
    }
    return header;
}

[[nodiscard]] inline std::vector<std::byte> copy_bytes(std::span<const std::byte> bytes) {
    return std::vector<std::byte>{bytes.begin(), bytes.end()};
}

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

template <bool Borrowed, class Extra>
[[nodiscard]] inline expected<byte_segments, status_code>
make_sign1_tbs(const basic_cose_sign1<Borrowed, Extra> &message, std::span<const std::byte> external_aad = {},
               std::optional<std::span<const std::byte>> detached_payload = std::nullopt) {
    if (!message.payload && !detached_payload) {
        return unexpected<status_code>{status_code::error};
    }
    const sig_structure_view structure{.context        = "Signature1",
                                       .body_protected = message.protected_header,
                                       .sign_protected = std::nullopt,
                                       .external_aad   = external_aad,
                                       .payload = message.payload ? std::span<const std::byte>{*message.payload} : *detached_payload};
    return detail::encode_signature_input(structure);
}

template <bool Borrowed, bool SignatureBorrowed, class BodyExtra, class SignerExtra, class SignatureExtra>
[[nodiscard]] inline expected<byte_segments, status_code>
make_sign_tbs(const basic_cose_sign<Borrowed, BodyExtra, SignerExtra>       &message,
              const basic_cose_signature<SignatureBorrowed, SignatureExtra> &signature, std::span<const std::byte> external_aad = {},
              std::optional<std::span<const std::byte>> detached_payload = std::nullopt) {
    if (!message.payload && !detached_payload) {
        return unexpected<status_code>{status_code::error};
    }
    const sig_structure_view structure{.context        = "Signature",
                                       .body_protected = message.protected_header,
                                       .sign_protected = std::span<const std::byte>{signature.protected_header},
                                       .external_aad   = external_aad,
                                       .payload = message.payload ? std::span<const std::byte>{*message.payload} : *detached_payload};
    return detail::encode_signature_input(structure);
}

// Returned segments borrow the models, so temporary models cannot be used here.
template <bool Borrowed, class Extra>
void make_sign1_tbs(const basic_cose_sign1<Borrowed, Extra> &&, std::span<const std::byte> = {},
                    std::optional<std::span<const std::byte>> = std::nullopt) = delete;

template <bool BodyBorrowed, bool SignatureBorrowed, class BodyExtra, class SignerExtra, class SignatureExtra>
void make_sign_tbs(const basic_cose_sign<BodyBorrowed, BodyExtra, SignerExtra> &&,
                   const basic_cose_signature<SignatureBorrowed, SignatureExtra> &, std::span<const std::byte> = {},
                   std::optional<std::span<const std::byte>> = std::nullopt) = delete;

template <bool BodyBorrowed, bool SignatureBorrowed, class BodyExtra, class SignerExtra, class SignatureExtra>
void make_sign_tbs(const basic_cose_sign<BodyBorrowed, BodyExtra, SignerExtra> &,
                   const basic_cose_signature<SignatureBorrowed, SignatureExtra> &&, std::span<const std::byte> = {},
                   std::optional<std::span<const std::byte>> = std::nullopt) = delete;

template <bool BodyBorrowed, bool SignatureBorrowed, class BodyExtra, class SignerExtra, class SignatureExtra>
void make_sign_tbs(const basic_cose_sign<BodyBorrowed, BodyExtra, SignerExtra> &&,
                   const basic_cose_signature<SignatureBorrowed, SignatureExtra> &&, std::span<const std::byte> = {},
                   std::optional<std::span<const std::byte>> = std::nullopt) = delete;

template <bool Borrowed, class Extra>
[[nodiscard]] inline expected<void, status_code>
validate_sign1_algorithm(const header_map &protected_header, const basic_header_map<Borrowed, Extra> &unprotected, algorithm expected) {
    if (detail::validate_header_pair(protected_header, unprotected) != status_code::success ||
        detail::validate_critical_labels(protected_header.crit, protected_header.alg.has_value(), protected_header.kid.has_value()) !=
            status_code::success) {
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
    if (detail::validate_header_pair(body_protected, body_unprotected) != status_code::success ||
        detail::validate_header_pair(signature_protected, signature_unprotected) != status_code::success ||
        detail::validate_critical_labels(body_protected.crit, body_protected.alg.has_value(), body_protected.kid.has_value()) !=
            status_code::success ||
        detail::validate_critical_labels(signature_protected.crit, signature_protected.alg.has_value(),
                                         signature_protected.kid.has_value()) != status_code::success) {
        return unexpected<status_code>{status_code::error};
    }
    // A COSE_Sign algorithm belongs to each signer, never the shared body.
    if (body_protected.alg) {
        return unexpected<status_code>{status_code::error};
    }
    if (signature_protected.alg && *signature_protected.alg != expected) {
        return unexpected<status_code>{status_code::error};
    }
    return {};
}

// Sign existing protected bytes without changing the message or owning its payload.
template <typename Backend, typename SigningKey, typename Message>
    requires std::same_as<std::remove_cvref_t<Message>, cose_sign1_view>
[[nodiscard]] inline expected<std::size_t, status_code> sign1(SigningKey &&key, Message &&message, std::span<std::byte> signature_output,
                                                              std::span<const std::byte>                external_aad     = {},
                                                              std::optional<std::span<const std::byte>> detached_payload = std::nullopt) {
    auto protected_header = decode_protected_header(message.protected_header);
    if (!protected_header) {
        return unexpected<status_code>{protected_header.error()};
    }
    auto status = validate_sign1_algorithm(*protected_header, message.unprotected, Backend::algorithm_id);
    if (!status) {
        return unexpected<status_code>{status.error()};
    }
    if (!protected_header->alg) {
        return unexpected<status_code>{status_code::error};
    }

    auto input = make_sign1_tbs(message, external_aad, detached_payload);
    if (!input) {
        return unexpected<status_code>{input.error()};
    }
    return Backend::sign(std::forward<SigningKey>(key), detail::signature_parts(*input), signature_output);
}

template <typename Backend, typename SigningKey, typename Message, typename Signer>
    requires std::same_as<std::remove_cvref_t<Message>, cose_sign_view> && std::same_as<std::remove_cvref_t<Signer>, cose_signature_view>
[[nodiscard]] inline expected<std::size_t, status_code>
sign_signature(SigningKey &&key, Message &&message, Signer &&signer, std::span<std::byte> signature_output,
               std::span<const std::byte> external_aad = {}, std::optional<std::span<const std::byte>> detached_payload = std::nullopt) {
    auto body_header = decode_protected_header(message.protected_header);
    if (!body_header) {
        return unexpected<status_code>{body_header.error()};
    }
    auto signer_header = decode_protected_header(signer.protected_header);
    if (!signer_header) {
        return unexpected<status_code>{signer_header.error()};
    }
    auto status = validate_sign_algorithm(*body_header, message.unprotected, *signer_header, signer.unprotected, Backend::algorithm_id);
    if (!status) {
        return unexpected<status_code>{status.error()};
    }
    if (!body_header->alg && !signer_header->alg) {
        return unexpected<status_code>{status_code::error};
    }

    auto input = make_sign_tbs(message, signer, external_aad, detached_payload);
    if (!input) {
        return unexpected<status_code>{input.error()};
    }
    return Backend::sign(std::forward<SigningKey>(key), detail::signature_parts(*input), signature_output);
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

    auto signature = Backend::sign(std::forward<SigningKey>(key), detail::signature_parts(*to_be_signed));
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

    auto signature_bytes = Backend::sign(std::forward<SigningKey>(key), detail::signature_parts(*to_be_signed));
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

namespace detail {
template <typename Backend, typename VerificationKey, bool Borrowed>
[[nodiscard]] inline expected<void, status_code>
verify_sign1_with_policy(VerificationKey &&key, const basic_cose_sign1<Borrowed> &message, verification_algorithm_policy policy,
                         std::span<const std::byte> external_aad, std::optional<std::span<const std::byte>> detached_payload) {
    auto protected_header = decode_protected_header(message.protected_header);
    if (!protected_header) {
        return unexpected<status_code>{protected_header.error()};
    }
    auto algorithm_status = validate_sign1_algorithm(*protected_header, message.unprotected, Backend::algorithm_id);
    if (!algorithm_status) {
        return unexpected<status_code>{algorithm_status.error()};
    }
    algorithm_status = validate_verification_algorithm(protected_header->alg, Backend::algorithm_id, policy);
    if (!algorithm_status) {
        return unexpected<status_code>{algorithm_status.error()};
    }

    auto to_be_signed = make_sign1_tbs(message, external_aad, detached_payload);
    if (!to_be_signed) {
        return unexpected<status_code>{to_be_signed.error()};
    }

    return Backend::verify(std::forward<VerificationKey>(key), detail::signature_parts(*to_be_signed),
                           std::span<const std::byte>{message.signature.data(), message.signature.size()});
}

template <typename Backend, typename VerificationKey, bool Borrowed, bool SignatureBorrowed>
[[nodiscard]] inline expected<void, status_code>
verify_signature_with_policy(VerificationKey &&key, const basic_cose_sign<Borrowed> &message,
                             const basic_cose_signature<SignatureBorrowed> &signature, verification_algorithm_policy policy,
                             std::span<const std::byte> external_aad, std::optional<std::span<const std::byte>> detached_payload) {
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
    algorithm_status = validate_verification_algorithm(signature_protected->alg, Backend::algorithm_id, policy);
    if (!algorithm_status) {
        return unexpected<status_code>{algorithm_status.error()};
    }

    auto to_be_signed = make_sign_tbs(message, signature, external_aad, detached_payload);
    if (!to_be_signed) {
        return unexpected<status_code>{to_be_signed.error()};
    }

    return Backend::verify(std::forward<VerificationKey>(key), detail::signature_parts(*to_be_signed),
                           std::span<const std::byte>{signature.signature.data(), signature.signature.size()});
}

template <typename Backend, typename VerificationKey, bool Borrowed>
[[nodiscard]] inline expected<void, status_code> verify_sign_with_policy(VerificationKey &&key, const basic_cose_sign<Borrowed> &message,
                                                                         verification_algorithm_policy policy, std::size_t signature_index,
                                                                         std::span<const std::byte>                external_aad,
                                                                         std::optional<std::span<const std::byte>> detached_payload) {
    if (signature_index >= message.signatures.size()) {
        return unexpected<status_code>{status_code::unexpected_group_size};
    }
    return verify_signature_with_policy<Backend>(std::forward<VerificationKey>(key), message, message.signatures[signature_index], policy,
                                                 external_aad, detached_payload);
}

template <typename Backend, typename VerificationKey, bool Borrowed>
[[nodiscard]] inline expected<void, status_code>
verify_sign_with_policy(VerificationKey &&key, const basic_cose_sign<Borrowed> &message, verification_algorithm_policy policy,
                        std::span<const std::byte> external_aad, std::optional<std::span<const std::byte>> detached_payload) {
    if (message.signatures.empty()) {
        return unexpected<status_code>{status_code::unexpected_group_size};
    }
    for (const auto &signature : message.signatures) {
        auto result = verify_signature_with_policy<Backend>(key, message, signature, policy, external_aad, detached_payload);
        if (!result) {
            return result;
        }
    }
    return {};
}
} // namespace detail

template <typename Backend, typename VerificationKey, bool Borrowed>
[[nodiscard]] inline expected<void, status_code> verify_sign1(VerificationKey &&key, const basic_cose_sign1<Borrowed> &message,
                                                              std::span<const std::byte>                external_aad     = {},
                                                              std::optional<std::span<const std::byte>> detached_payload = std::nullopt) {
    return detail::verify_sign1_with_policy<Backend>(std::forward<VerificationKey>(key), message,
                                                     detail::verification_algorithm_policy::explicit_algorithm, external_aad,
                                                     detached_payload);
}

template <typename Backend, typename VerificationKey, bool Borrowed>
[[nodiscard]] inline expected<void, status_code> verify_sign1(VerificationKey &&key, const basic_cose_sign1<Borrowed> &message,
                                                              implicit_algorithm_profile_t, std::span<const std::byte> external_aad = {},
                                                              std::optional<std::span<const std::byte>> detached_payload = std::nullopt) {
    return detail::verify_sign1_with_policy<Backend>(std::forward<VerificationKey>(key), message,
                                                     detail::verification_algorithm_policy::implicit_algorithm, external_aad,
                                                     detached_payload);
}

template <typename Backend, typename VerificationKey, bool Borrowed, bool SignatureBorrowed>
[[nodiscard]] inline expected<void, status_code>
verify_signature(VerificationKey &&key, const basic_cose_sign<Borrowed> &message, const basic_cose_signature<SignatureBorrowed> &signature,
                 std::span<const std::byte> external_aad = {}, std::optional<std::span<const std::byte>> detached_payload = std::nullopt) {
    return detail::verify_signature_with_policy<Backend>(std::forward<VerificationKey>(key), message, signature,
                                                         detail::verification_algorithm_policy::explicit_algorithm, external_aad,
                                                         detached_payload);
}

template <typename Backend, typename VerificationKey, bool Borrowed, bool SignatureBorrowed>
[[nodiscard]] inline expected<void, status_code>
verify_signature(VerificationKey &&key, const basic_cose_sign<Borrowed> &message, const basic_cose_signature<SignatureBorrowed> &signature,
                 implicit_algorithm_profile_t, std::span<const std::byte> external_aad = {},
                 std::optional<std::span<const std::byte>> detached_payload = std::nullopt) {
    return detail::verify_signature_with_policy<Backend>(std::forward<VerificationKey>(key), message, signature,
                                                         detail::verification_algorithm_policy::implicit_algorithm, external_aad,
                                                         detached_payload);
}

template <typename Backend, typename VerificationKey, bool Borrowed>
[[nodiscard]] inline expected<void, status_code> verify_sign(VerificationKey &&key, const basic_cose_sign<Borrowed> &message,
                                                             std::size_t signature_index, std::span<const std::byte> external_aad = {},
                                                             std::optional<std::span<const std::byte>> detached_payload = std::nullopt) {
    return detail::verify_sign_with_policy<Backend>(std::forward<VerificationKey>(key), message,
                                                    detail::verification_algorithm_policy::explicit_algorithm, signature_index,
                                                    external_aad, detached_payload);
}

template <typename Backend, typename VerificationKey, bool Borrowed>
[[nodiscard]] inline expected<void, status_code>
verify_sign(VerificationKey &&key, const basic_cose_sign<Borrowed> &message, std::size_t signature_index, implicit_algorithm_profile_t,
            std::span<const std::byte> external_aad = {}, std::optional<std::span<const std::byte>> detached_payload = std::nullopt) {
    return detail::verify_sign_with_policy<Backend>(std::forward<VerificationKey>(key), message,
                                                    detail::verification_algorithm_policy::implicit_algorithm, signature_index,
                                                    external_aad, detached_payload);
}

template <typename Backend, typename VerificationKey, bool Borrowed>
[[nodiscard]] inline expected<void, status_code> verify_sign(VerificationKey &&key, const basic_cose_sign<Borrowed> &message,
                                                             std::span<const std::byte>                external_aad     = {},
                                                             std::optional<std::span<const std::byte>> detached_payload = std::nullopt) {
    return detail::verify_sign_with_policy<Backend>(std::forward<VerificationKey>(key), message,
                                                    detail::verification_algorithm_policy::explicit_algorithm, external_aad,
                                                    detached_payload);
}

template <typename Backend, typename VerificationKey, bool Borrowed>
[[nodiscard]] inline expected<void, status_code> verify_sign(VerificationKey &&key, const basic_cose_sign<Borrowed>  &message,
                                                             implicit_algorithm_profile_t, std::span<const std::byte> external_aad = {},
                                                             std::optional<std::span<const std::byte>> detached_payload = std::nullopt) {
    return detail::verify_sign_with_policy<Backend>(std::forward<VerificationKey>(key), message,
                                                    detail::verification_algorithm_policy::implicit_algorithm, external_aad,
                                                    detached_payload);
}

template <bool Borrowed> [[nodiscard]] inline auto as_cose_sign1(basic_cose_sign1<Borrowed> value) {
    return make_tag_pair(cose_sign1_tag{}, std::move(value));
}
template <bool Borrowed> [[nodiscard]] inline auto as_cose_sign(basic_cose_sign<Borrowed> value) {
    return make_tag_pair(cose_sign_tag{}, std::move(value));
}

template <typename T> [[nodiscard]] inline auto as_cwt(T value) { return make_tag_pair(cwt_tag{}, std::move(value)); }

} // namespace cbor::tags::cwt

#include "cbor_tags/cwt/headers.h"
