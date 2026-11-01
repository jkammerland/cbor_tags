#pragma once

#include "cbor_tags/cwt/cwt.h"

namespace cbor::tags::cwt {

enum class header_scope { body, signature };

namespace detail {

struct reject_critical {
    template <class Header> expected<void, status_code> operator()(header_scope, header_label_view, const Header &) const {
        return unexpected<status_code>{status_code::error};
    }
};

template <class Header, class Processor>
expected<void, status_code> process_critical_headers(header_scope scope, const Header &header, Processor &process) {
    for (const auto &label : header.crit) {
        const auto owned = owned_label(label);
        if (is_header_label(owned, 1) || is_header_label(owned, 4)) {
            continue;
        }
        auto view = std::visit(
            [](const auto &value) -> header_label_view {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, integer>) {
                    return value;
                } else {
                    return std::string_view{value};
                }
            },
            label);
        auto result = process(scope, view, header);
        if (!result) {
            return unexpected<status_code>{result.error()};
        }
    }
    return {};
}

template <template <class> class... Codecs, class Message, class Header>
expected<void, status_code> read_sign1_headers(const Message &message, Header &header, algorithm expected_algorithm) {
    auto parsed = decode_protected_header<Codecs...>(message.protected_header, header);
    if (!parsed) {
        return unexpected<status_code>{parsed.error()};
    }
    if (validate_header_pair(header, message.unprotected) != status_code::success || (header.alg && *header.alg != expected_algorithm)) {
        return unexpected<status_code>{status_code::error};
    }
    return {};
}

template <template <class> class... Codecs, class Message, class Signature, class Body, class Signer>
expected<void, status_code> read_sign_headers(const Message &message, const Signature &signature, Body &body, Signer &signer,
                                              algorithm expected_algorithm) {
    auto parsed = decode_protected_header<Codecs...>(message.protected_header, body);
    if (!parsed) {
        return unexpected<status_code>{parsed.error()};
    }
    parsed = decode_protected_header<Codecs...>(signature.protected_header, signer);
    if (!parsed) {
        return unexpected<status_code>{parsed.error()};
    }
    if (validate_header_pair(body, message.unprotected) != status_code::success || body.alg ||
        validate_header_pair(signer, signature.unprotected) != status_code::success || (signer.alg && *signer.alg != expected_algorithm)) {
        return unexpected<status_code>{status_code::error};
    }
    return {};
}

// Parsed views are published only after crypto and critical processing succeed.
template <typename Backend, template <class> class... Codecs, typename Key, bool Borrowed, class Extra, bool HeaderBorrowed,
          class HeaderExtra, class Processor>
[[nodiscard]] expected<void, status_code> verify_sign1_with_headers(Key &&key, const basic_cose_sign1<Borrowed, Extra> &message,
                                                                    basic_header_map<HeaderBorrowed, HeaderExtra> &authenticated,
                                                                    Processor &&process, verification_algorithm_policy policy,
                                                                    std::span<const std::byte>                aad,
                                                                    std::optional<std::span<const std::byte>> detached) {
    basic_header_map<HeaderBorrowed, HeaderExtra> parsed;
    auto                                          status = detail::read_sign1_headers<Codecs...>(message, parsed, Backend::algorithm_id);
    if (!status) {
        return unexpected<status_code>{status.error()};
    }
    status = validate_verification_algorithm(parsed.alg, Backend::algorithm_id, policy);
    if (!status) {
        return unexpected<status_code>{status.error()};
    }
    auto input = make_sign1_tbs(message, aad, detached);
    if (!input) {
        return unexpected<status_code>{input.error()};
    }
    status = Backend::verify(std::forward<Key>(key), detail::signature_parts(*input), message.signature);
    if (!status) {
        return unexpected<status_code>{status.error()};
    }
    status = detail::process_critical_headers(header_scope::body, parsed, process);
    if (!status) {
        return unexpected<status_code>{status.error()};
    }
    authenticated = std::move(parsed);
    return {};
}
} // namespace detail

template <typename Backend, template <class> class... Codecs, typename Key, bool Borrowed, class Extra, bool HeaderBorrowed,
          class HeaderExtra, class Processor>
[[nodiscard]] expected<void, status_code>
verify_sign1(Key &&key, const basic_cose_sign1<Borrowed, Extra> &message, basic_header_map<HeaderBorrowed, HeaderExtra> &authenticated,
             Processor &&process, std::span<const std::byte> aad = {}, std::optional<std::span<const std::byte>> detached = std::nullopt) {
    return detail::verify_sign1_with_headers<Backend, Codecs...>(std::forward<Key>(key), message, authenticated,
                                                                 std::forward<Processor>(process),
                                                                 detail::verification_algorithm_policy::explicit_algorithm, aad, detached);
}

template <typename Backend, template <class> class... Codecs, typename Key, bool Borrowed, class Extra, bool HeaderBorrowed,
          class HeaderExtra, class Processor>
[[nodiscard]] expected<void, status_code>
verify_sign1(Key &&key, const basic_cose_sign1<Borrowed, Extra> &message, implicit_algorithm_profile_t,
             basic_header_map<HeaderBorrowed, HeaderExtra> &authenticated, Processor &&process, std::span<const std::byte> aad = {},
             std::optional<std::span<const std::byte>> detached = std::nullopt) {
    return detail::verify_sign1_with_headers<Backend, Codecs...>(std::forward<Key>(key), message, authenticated,
                                                                 std::forward<Processor>(process),
                                                                 detail::verification_algorithm_policy::implicit_algorithm, aad, detached);
}

template <typename Backend, template <class> class... Codecs, typename Key, bool Borrowed, class Extra, bool HeaderBorrowed,
          class HeaderExtra>
[[nodiscard]] expected<std::size_t, status_code>
sign1(Key &&key, const basic_cose_sign1<Borrowed, Extra> &message, basic_header_map<HeaderBorrowed, HeaderExtra> &parsed_header,
      std::span<std::byte> output, std::span<const std::byte> aad = {}, std::optional<std::span<const std::byte>> detached = std::nullopt) {
    basic_header_map<HeaderBorrowed, HeaderExtra> parsed;
    auto                                          status = detail::read_sign1_headers<Codecs...>(message, parsed, Backend::algorithm_id);
    if (!status) {
        return unexpected<status_code>{status.error()};
    }
    if (!parsed.alg) {
        return unexpected<status_code>{status_code::error};
    }
    auto input = make_sign1_tbs(message, aad, detached);
    if (!input) {
        return unexpected<status_code>{input.error()};
    }
    auto written = Backend::sign(std::forward<Key>(key), detail::signature_parts(*input), output);
    if (!written) {
        return unexpected<status_code>{written.error()};
    }
    parsed_header = std::move(parsed);
    return *written;
}

namespace detail {
template <typename Backend, template <class> class... Codecs, typename Key, bool Borrowed, class BodyExtra, class SignerExtra,
          bool SignatureBorrowed, class SignatureExtra, class Body, class Signer, class Processor>
[[nodiscard]] expected<void, status_code>
verify_signature_with_headers(Key &&key, const basic_cose_sign<Borrowed, BodyExtra, SignerExtra> &message,
                              const basic_cose_signature<SignatureBorrowed, SignatureExtra> &signature, Body &authenticated_body,
                              Signer &authenticated_signer, Processor &&process, verification_algorithm_policy policy,
                              std::span<const std::byte> aad, std::optional<std::span<const std::byte>> detached) {
    Body   body;
    Signer signer;
    auto   status = detail::read_sign_headers<Codecs...>(message, signature, body, signer, Backend::algorithm_id);
    if (!status) {
        return unexpected<status_code>{status.error()};
    }
    status = validate_verification_algorithm(signer.alg, Backend::algorithm_id, policy);
    if (!status) {
        return unexpected<status_code>{status.error()};
    }
    auto input = make_sign_tbs(message, signature, aad, detached);
    if (!input) {
        return unexpected<status_code>{input.error()};
    }
    status = Backend::verify(std::forward<Key>(key), detail::signature_parts(*input), signature.signature);
    if (!status) {
        return unexpected<status_code>{status.error()};
    }
    status = detail::process_critical_headers(header_scope::body, body, process);
    if (!status) {
        return unexpected<status_code>{status.error()};
    }
    status = detail::process_critical_headers(header_scope::signature, signer, process);
    if (!status) {
        return unexpected<status_code>{status.error()};
    }
    authenticated_body   = std::move(body);
    authenticated_signer = std::move(signer);
    return {};
}
} // namespace detail

template <typename Backend, template <class> class... Codecs, typename Key, bool Borrowed, class BodyExtra, class SignerExtra,
          bool SignatureBorrowed, class SignatureExtra, class Body, class Signer, class Processor>
[[nodiscard]] expected<void, status_code> verify_signature(Key &&key, const basic_cose_sign<Borrowed, BodyExtra, SignerExtra> &message,
                                                           const basic_cose_signature<SignatureBorrowed, SignatureExtra> &signature,
                                                           Body &authenticated_body, Signer &authenticated_signer, Processor &&process,
                                                           std::span<const std::byte>                aad      = {},
                                                           std::optional<std::span<const std::byte>> detached = std::nullopt) {
    return detail::verify_signature_with_headers<Backend, Codecs...>(
        std::forward<Key>(key), message, signature, authenticated_body, authenticated_signer, std::forward<Processor>(process),
        detail::verification_algorithm_policy::explicit_algorithm, aad, detached);
}

template <typename Backend, template <class> class... Codecs, typename Key, bool Borrowed, class BodyExtra, class SignerExtra,
          bool SignatureBorrowed, class SignatureExtra, class Body, class Signer, class Processor>
[[nodiscard]] expected<void, status_code>
verify_signature(Key &&key, const basic_cose_sign<Borrowed, BodyExtra, SignerExtra> &message,
                 const basic_cose_signature<SignatureBorrowed, SignatureExtra> &signature, implicit_algorithm_profile_t,
                 Body &authenticated_body, Signer &authenticated_signer, Processor &&process, std::span<const std::byte> aad = {},
                 std::optional<std::span<const std::byte>> detached = std::nullopt) {
    return detail::verify_signature_with_headers<Backend, Codecs...>(
        std::forward<Key>(key), message, signature, authenticated_body, authenticated_signer, std::forward<Processor>(process),
        detail::verification_algorithm_policy::implicit_algorithm, aad, detached);
}

template <typename Backend, template <class> class... Codecs, typename Key, bool Borrowed, class BodyExtra, class SignerExtra,
          bool SignatureBorrowed, class SignatureExtra, class Body, class Signer>
[[nodiscard]] expected<std::size_t, status_code> sign_signature(Key &&key, const basic_cose_sign<Borrowed, BodyExtra, SignerExtra> &message,
                                                                const basic_cose_signature<SignatureBorrowed, SignatureExtra> &signature,
                                                                Body &parsed_body, Signer &parsed_signer, std::span<std::byte> output,
                                                                std::span<const std::byte>                aad      = {},
                                                                std::optional<std::span<const std::byte>> detached = std::nullopt) {
    Body   body;
    Signer signer;
    auto   status = detail::read_sign_headers<Codecs...>(message, signature, body, signer, Backend::algorithm_id);
    if (!status) {
        return unexpected<status_code>{status.error()};
    }
    if (!signer.alg) {
        return unexpected<status_code>{status_code::error};
    }
    auto input = make_sign_tbs(message, signature, aad, detached);
    if (!input) {
        return unexpected<status_code>{input.error()};
    }
    auto written = Backend::sign(std::forward<Key>(key), detail::signature_parts(*input), output);
    if (!written) {
        return unexpected<status_code>{written.error()};
    }
    parsed_body   = std::move(body);
    parsed_signer = std::move(signer);
    return *written;
}

template <typename Backend, template <class> class... Codecs, typename Key, bool Borrowed, class BodyExtra, class SignerExtra, class Body,
          class Signer, class Processor>
[[nodiscard]] expected<void, status_code>
verify_sign(Key &&key, const basic_cose_sign<Borrowed, BodyExtra, SignerExtra> &message, std::size_t index, Body &body, Signer &signer,
            Processor &&process, std::span<const std::byte> aad = {}, std::optional<std::span<const std::byte>> detached = std::nullopt) {
    if (index >= message.signatures.size()) {
        return unexpected<status_code>{status_code::error};
    }
    return verify_signature<Backend, Codecs...>(std::forward<Key>(key), message, message.signatures[index], body, signer,
                                                std::forward<Processor>(process), aad, detached);
}

template <typename Backend, template <class> class... Codecs, typename Key, bool Borrowed, class BodyExtra, class SignerExtra, class Body,
          class Signer, class Processor>
[[nodiscard]] expected<void, status_code> verify_sign(Key &&key, const basic_cose_sign<Borrowed, BodyExtra, SignerExtra> &message,
                                                      std::size_t index, implicit_algorithm_profile_t, Body &body, Signer &signer,
                                                      Processor &&process, std::span<const std::byte> aad = {},
                                                      std::optional<std::span<const std::byte>> detached = std::nullopt) {
    if (index >= message.signatures.size()) {
        return unexpected<status_code>{status_code::error};
    }
    return verify_signature<Backend, Codecs...>(std::forward<Key>(key), message, message.signatures[index], implicit_algorithm_profile,
                                                body, signer, std::forward<Processor>(process), aad, detached);
}

// These overloads return parsed views; named owners are required even for crypto calls.
template <typename Backend, template <class> class... Codecs, typename Key, bool B, class E, class... Args>
void verify_sign1(Key &&, const basic_cose_sign1<B, E> &&, Args &&...) = delete;
template <typename Backend, template <class> class... Codecs, typename Key, bool B, class E, class... Args>
void sign1(Key &&, const basic_cose_sign1<B, E> &&, Args &&...) = delete;
template <typename Backend, template <class> class... Codecs, typename Key, bool B, class E, class S, class... Args>
void verify_signature(Key &&, const basic_cose_sign<B, E, S> &&, Args &&...) = delete;
template <typename Backend, template <class> class... Codecs, typename Key, bool B, class E, class S, bool SB, class SE, class... Args>
void verify_signature(Key &&, const basic_cose_sign<B, E, S> &, const basic_cose_signature<SB, SE> &&, Args &&...) = delete;

template <typename Backend, template <class> class... Codecs, typename Key, bool B, class E, class S, class... Args>
void sign_signature(Key &&, const basic_cose_sign<B, E, S> &&, Args &&...) = delete;
template <typename Backend, template <class> class... Codecs, typename Key, bool B, class E, class S, bool SB, class SE, class... Args>
void sign_signature(Key &&, const basic_cose_sign<B, E, S> &, const basic_cose_signature<SB, SE> &&, Args &&...) = delete;
template <typename Backend, template <class> class... Codecs, typename Key, bool B, class E, class S, class... Args>
void verify_sign(Key &&, const basic_cose_sign<B, E, S> &&, Args &&...) = delete;

} // namespace cbor::tags::cwt
