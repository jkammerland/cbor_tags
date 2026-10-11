#pragma once

#include "cbor_tags/cwt/countersignature_types.h"
#include "cbor_tags/cwt/headers.h"

namespace cbor::tags::cwt {
namespace detail {

template <typename T>
concept CountersignatureTarget = requires(const T &target) {
    std::span<const std::byte>{target.protected_header};
    std::span<const std::byte>{target.signature};
};

// Signature targets use their third field; Sign1 additionally binds its fourth.
template <CountersignatureTarget Target>
expected<byte_segments, status_code>
countersignature_input(Target &target, std::optional<std::span<const std::byte>> countersignature_protected, std::span<const std::byte> aad,
                       std::optional<std::span<const std::byte>> detached, bool legacy = false) {
    constexpr bool         sign1 = requires { target.payload; };
    const bool             other = sign1 && !legacy;
    const bool             full  = countersignature_protected.has_value();
    const std::string_view context =
        full ? (other ? "CounterSignatureV2" : "CounterSignature") : (other ? "CounterSignature0V2" : "CounterSignature0");
    std::span<const std::byte> payload;
    if constexpr (sign1) {
        if (!target.payload && !detached) {
            return unexpected<status_code>{status_code::error};
        }
        payload = target.payload ? std::span<const std::byte>{*target.payload} : *detached;
    } else {
        if (detached) {
            return unexpected<status_code>{status_code::error};
        }
        payload = target.signature;
    }

    byte_segments segments;
    auto          encoder = make_encoder<codec::cwt>(segments);
    auto          result =
        encoder(as_array{static_cast<std::uint64_t>(4 + full + other)}, context, std::span<const std::byte>{target.protected_header});
    if (!result) {
        return unexpected<status_code>{result.error()};
    }
    if (full) {
        result = encoder(*countersignature_protected);
        if (!result) {
            return unexpected<status_code>{result.error()};
        }
    }
    result = encoder(aad, payload);
    if (!result) {
        return unexpected<status_code>{result.error()};
    }
    if (other) {
        result = encoder(as_array{1}, std::span<const std::byte>{target.signature});
        if (!result) {
            return unexpected<status_code>{result.error()};
        }
    }
    return segments;
}

template <typename Backend, typename Countersignature>
expected<void, status_code> validate_countersignature_headers(const Countersignature &countersignature) {
    auto header = decode_protected_header(countersignature.protected_header);
    if (!header) {
        return unexpected<status_code>{header.error()};
    }
    return validate_sign1_algorithm(*header, countersignature.unprotected, Backend::algorithm_id);
}

} // namespace detail

// Returned segments borrow only named models and the caller's AAD/payload buffers.
template <detail::CountersignatureTarget Target, bool B, typename E>
[[nodiscard]] expected<byte_segments, status_code> make_countersign_tbs(Target &target, const basic_cose_signature<B, E> &countersignature,
                                                                        std::span<const std::byte>                aad      = {},
                                                                        std::optional<std::span<const std::byte>> detached = std::nullopt) {
    return detail::countersignature_input(target, std::span<const std::byte>{countersignature.protected_header}, aad, detached);
}

template <detail::CountersignatureTarget Target, bool B, typename E>
void make_countersign_tbs(Target &, const basic_cose_signature<B, E> &&, std::span<const std::byte> = {},
                          std::optional<std::span<const std::byte>> = std::nullopt) = delete;

template <detail::CountersignatureTarget Target>
[[nodiscard]] expected<byte_segments, status_code>
make_countersign0_tbs(Target &target, std::span<const std::byte> aad = {},
                      std::optional<std::span<const std::byte>> detached = std::nullopt) {
    return detail::countersignature_input(target, std::nullopt, aad, detached);
}

// Caller storage is separate from the target and is never attached automatically.
template <typename Backend, typename Key, detail::CountersignatureTarget Target, bool B, typename E>
[[nodiscard]] expected<std::size_t, status_code> countersign(Key &&key, Target &target, const basic_cose_signature<B, E> &countersignature,
                                                             std::span<std::byte> output, std::span<const std::byte> aad = {},
                                                             std::optional<std::span<const std::byte>> detached = std::nullopt) {
    auto valid = detail::validate_countersignature_headers<Backend>(countersignature);
    if (!valid) {
        return unexpected<status_code>{valid.error()};
    }
    auto input = make_countersign_tbs(target, countersignature, aad, detached);
    if (!input) {
        return unexpected<status_code>{input.error()};
    }
    return Backend::sign(std::forward<Key>(key), detail::signature_parts(*input), output);
}

template <typename Backend, typename Key, detail::CountersignatureTarget Target, typename E>
[[nodiscard]] expected<basic_cose_signature<false, E>, status_code>
countersign(Key &&key, Target &target, basic_cose_signature<false, E> countersignature, std::span<const std::byte> aad = {},
            std::optional<std::span<const std::byte>> detached = std::nullopt) {
    auto header = decode_protected_header(countersignature.protected_header);
    if (!header) {
        return unexpected<status_code>{header.error()};
    }
    if (!header->alg) {
        // Do not re-encode nonempty protected bytes merely to insert an algorithm.
        if (!countersignature.protected_header.empty()) {
            return unexpected<status_code>{status_code::error};
        }
        header->alg  = Backend::algorithm_id;
        auto encoded = encode_protected_header(*header);
        if (!encoded) {
            return unexpected<status_code>{encoded.error()};
        }
        countersignature.protected_header = std::move(*encoded);
    }
    auto valid = detail::validate_countersignature_headers<Backend>(countersignature);
    if (!valid) {
        return unexpected<status_code>{valid.error()};
    }
    auto input = make_countersign_tbs(target, countersignature, aad, detached);
    if (!input) {
        return unexpected<status_code>{input.error()};
    }
    auto signature = Backend::sign(std::forward<Key>(key), detail::signature_parts(*input));
    if (!signature) {
        return unexpected<status_code>{signature.error()};
    }
    countersignature.signature = std::move(*signature);
    return countersignature;
}

template <typename Backend, typename Key, detail::CountersignatureTarget Target, bool B, typename E>
[[nodiscard]] expected<void, status_code>
verify_countersignature(Key &&key, Target &target, const basic_cose_signature<B, E> &countersignature, std::span<const std::byte> aad = {},
                        std::optional<std::span<const std::byte>> detached = std::nullopt) {
    auto valid = detail::validate_countersignature_headers<Backend>(countersignature);
    if (!valid) {
        return valid;
    }
    auto input = make_countersign_tbs(target, countersignature, aad, detached);
    if (!input) {
        return unexpected<status_code>{input.error()};
    }
    return Backend::verify(std::forward<Key>(key), detail::signature_parts(*input), countersignature.signature);
}

// Legacy verification intentionally omits Sign1's original signature as specified by RFC 8152.
template <typename Backend, typename Key, detail::CountersignatureTarget Target, bool B, typename E>
[[nodiscard]] expected<void, status_code>
verify_legacy_countersignature(Key &&key, Target &target, const basic_cose_signature<B, E> &countersignature,
                               std::span<const std::byte> aad = {}, std::optional<std::span<const std::byte>> detached = std::nullopt) {
    auto valid = detail::validate_countersignature_headers<Backend>(countersignature);
    if (!valid) {
        return valid;
    }
    auto input = detail::countersignature_input(target, std::span<const std::byte>{countersignature.protected_header}, aad, detached, true);
    if (!input) {
        return unexpected<status_code>{input.error()};
    }
    return Backend::verify(std::forward<Key>(key), detail::signature_parts(*input), countersignature.signature);
}

template <typename Backend, typename Key, detail::CountersignatureTarget Target>
[[nodiscard]] expected<std::vector<std::byte>, status_code>
countersign0(Key &&key, Target &target, std::span<const std::byte> aad = {},
             std::optional<std::span<const std::byte>> detached = std::nullopt) {
    auto input = make_countersign0_tbs(target, aad, detached);
    if (!input) {
        return unexpected<status_code>{input.error()};
    }
    return Backend::sign(std::forward<Key>(key), detail::signature_parts(*input));
}

template <typename Backend, typename Key, detail::CountersignatureTarget Target, typename Output>
    requires std::convertible_to<Output, std::span<std::byte>>
[[nodiscard]] expected<std::size_t, status_code> countersign0(Key &&key, Target &target, Output &&output, std::span<const std::byte> aad,
                                                              std::optional<std::span<const std::byte>> detached = std::nullopt) {
    auto input = make_countersign0_tbs(target, aad, detached);
    if (!input) {
        return unexpected<status_code>{input.error()};
    }
    return Backend::sign(std::forward<Key>(key), detail::signature_parts(*input), std::span<std::byte>{output});
}

template <typename Backend, typename Key, detail::CountersignatureTarget Target>
[[nodiscard]] expected<void, status_code> verify_countersignature0(Key &&key, Target &target, std::span<const std::byte> signature,
                                                                   std::span<const std::byte>                aad      = {},
                                                                   std::optional<std::span<const std::byte>> detached = std::nullopt) {
    auto input = make_countersign0_tbs(target, aad, detached);
    if (!input) {
        return unexpected<status_code>{input.error()};
    }
    return Backend::verify(std::forward<Key>(key), detail::signature_parts(*input), signature);
}

// Parsed headers retain application codecs and critical processing.
template <typename Backend, template <class> class... Codecs, typename Key, detail::CountersignatureTarget Target, bool B, typename E,
          bool HB, typename HE>
[[nodiscard]] expected<std::size_t, status_code> countersign(Key &&key, Target &target, const basic_cose_signature<B, E> &countersignature,
                                                             basic_header_map<HB, HE> &parsed_header, std::span<std::byte> output,
                                                             std::span<const std::byte>                aad      = {},
                                                             std::optional<std::span<const std::byte>> detached = std::nullopt) {
    basic_header_map<HB, HE> parsed;
    auto                     status = detail::read_sign1_headers<Codecs...>(countersignature, parsed, Backend::algorithm_id);
    if (!status) {
        return unexpected<status_code>{status.error()};
    }
    if (!parsed.alg) {
        return unexpected<status_code>{status_code::error};
    }
    auto input = make_countersign_tbs(target, countersignature, aad, detached);
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
template <bool Legacy, typename Backend, template <class> class... Codecs, typename Key, CountersignatureTarget Target, bool B, typename E,
          bool HB, typename HE, typename Processor>
expected<void, status_code>
verify_countersignature_with_headers(Key &&key, Target &target, const basic_cose_signature<B, E> &countersignature,
                                     basic_header_map<HB, HE> &authenticated, Processor &&process, std::span<const std::byte> aad,
                                     std::optional<std::span<const std::byte>> detached) {
    basic_header_map<HB, HE> parsed;
    auto                     status = read_sign1_headers<Codecs...>(countersignature, parsed, Backend::algorithm_id);
    if (!status) {
        return unexpected<status_code>{status.error()};
    }
    auto input = countersignature_input(target, std::span<const std::byte>{countersignature.protected_header}, aad, detached, Legacy);
    if (!input) {
        return unexpected<status_code>{input.error()};
    }
    status = Backend::verify(std::forward<Key>(key), signature_parts(*input), countersignature.signature);
    if (!status) {
        return unexpected<status_code>{status.error()};
    }
    status = process_critical_headers(header_scope::signature, parsed, process);
    if (!status) {
        return unexpected<status_code>{status.error()};
    }
    authenticated = std::move(parsed);
    return {};
}
} // namespace detail

template <typename Backend, template <class> class... Codecs, typename Key, detail::CountersignatureTarget Target, bool B, typename E,
          bool HB, typename HE, typename Processor>
[[nodiscard]] expected<void, status_code>
verify_countersignature(Key &&key, Target &target, const basic_cose_signature<B, E> &countersignature,
                        basic_header_map<HB, HE> &authenticated, Processor &&process, std::span<const std::byte> aad = {},
                        std::optional<std::span<const std::byte>> detached = std::nullopt) {
    return detail::verify_countersignature_with_headers<false, Backend, Codecs...>(
        std::forward<Key>(key), target, countersignature, authenticated, std::forward<Processor>(process), aad, detached);
}

template <typename Backend, template <class> class... Codecs, typename Key, detail::CountersignatureTarget Target, bool B, typename E,
          bool HB, typename HE, typename Processor>
[[nodiscard]] expected<void, status_code>
verify_legacy_countersignature(Key &&key, Target &target, const basic_cose_signature<B, E> &countersignature,
                               basic_header_map<HB, HE> &authenticated, Processor &&process, std::span<const std::byte> aad = {},
                               std::optional<std::span<const std::byte>> detached = std::nullopt) {
    return detail::verify_countersignature_with_headers<true, Backend, Codecs...>(
        std::forward<Key>(key), target, countersignature, authenticated, std::forward<Processor>(process), aad, detached);
}

// Parsed output may borrow the countersignature's protected bytes.
template <typename Backend, template <class> class... Codecs, typename Key, detail::CountersignatureTarget Target, bool B, typename E,
          bool HB, typename HE, typename... Args>
void countersign(Key &&, Target &, const basic_cose_signature<B, E> &&, basic_header_map<HB, HE> &, Args &&...) = delete;
template <typename Backend, template <class> class... Codecs, typename Key, detail::CountersignatureTarget Target, bool B, typename E,
          bool HB, typename HE, typename... Args>
void verify_countersignature(Key &&, Target &, const basic_cose_signature<B, E> &&, basic_header_map<HB, HE> &, Args &&...) = delete;
template <typename Backend, template <class> class... Codecs, typename Key, detail::CountersignatureTarget Target, bool B, typename E,
          bool HB, typename HE, typename... Args>
void verify_legacy_countersignature(Key &&, Target &, const basic_cose_signature<B, E> &&, basic_header_map<HB, HE> &, Args &&...) = delete;

} // namespace cbor::tags::cwt
