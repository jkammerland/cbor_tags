#pragma once

#include "someip/ser/detail.h"

namespace someip::ser::detail {

template <class Out, class T>
expected<void> encode_impl(wire::writer<Out> &out, const config &cfg, const T &value, std::size_t base_offset) noexcept;

template <class T>
expected<void> measure_impl(sizer &s, const config &cfg, const T &value, std::size_t base_offset) noexcept;

template <class Out>
expected<void> encode_scalar(wire::writer<Out> &out, const config &cfg, const std::byte v) noexcept {
    (void)cfg;
    return out.write_byte(v);
}

template <class Out>
expected<void> encode_scalar(wire::writer<Out> &out, const config &cfg, bool v) noexcept {
    (void)cfg;
    return out.write_byte(v ? std::byte{0x01} : std::byte{0x00});
}

template <class Out, class T>
    requires(is_integral_not_bool<T> && std::is_unsigned_v<std::remove_cvref_t<T>>)
expected<void> encode_scalar(wire::writer<Out> &out, const config &cfg, T v) noexcept {
    return wire::write_uint(cfg.payload_endian, out, static_cast<std::make_unsigned_t<std::remove_cvref_t<T>>>(v));
}

template <class Out, class T>
    requires(is_integral_not_bool<T> && std::is_signed_v<std::remove_cvref_t<T>>)
expected<void> encode_scalar(wire::writer<Out> &out, const config &cfg, T v) noexcept {
    using U = std::make_unsigned_t<std::remove_cvref_t<T>>;
    return wire::write_uint(cfg.payload_endian, out, static_cast<U>(v));
}

template <class Out, class T>
    requires(is_enum<T>)
expected<void> encode_scalar(wire::writer<Out> &out, const config &cfg, T v) noexcept {
    using U = std::underlying_type_t<std::remove_cvref_t<T>>;
    return encode_scalar(out, cfg, static_cast<U>(v));
}

template <class Out>
expected<void> encode_scalar(wire::writer<Out> &out, const config &cfg, float v) noexcept {
    const auto bits = std::bit_cast<std::uint32_t>(v);
    return wire::write_uint(cfg.payload_endian, out, bits);
}

template <class Out>
expected<void> encode_scalar(wire::writer<Out> &out, const config &cfg, double v) noexcept {
    const auto bits = std::bit_cast<std::uint64_t>(v);
    return wire::write_uint(cfg.payload_endian, out, bits);
}

template <class Out, std::size_t N>
expected<void> encode_impl(wire::writer<Out> &out, const config &cfg, const std::array<std::byte, N> &value,
                           std::size_t base_offset) noexcept {
    (void)cfg;
    (void)base_offset;
    return out.write_bytes(std::span<const std::byte>(value.data(), value.size()));
}

template <class Out, class T, std::size_t N>
expected<void> encode_impl(wire::writer<Out> &out, const config &cfg, const std::array<T, N> &value,
                           std::size_t base_offset) noexcept {
    for (const auto &e : value) {
        auto st = encode_impl(out, cfg, e, base_offset);
        if (!st) return st;
    }
    return {};
}

template <std::size_t N>
expected<void> measure_impl(sizer &s, const config &cfg, const std::array<std::byte, N> &, std::size_t base_offset) noexcept {
    (void)cfg;
    (void)base_offset;
    s.advance(N);
    return {};
}

template <class T, std::size_t N>
expected<void> measure_impl(sizer &s, const config &cfg, const std::array<T, N> &v, std::size_t base_offset) noexcept {
    for (const auto &e : v) {
        auto st = measure_impl(s, cfg, e, base_offset);
        if (!st) return st;
    }
    return {};
}

template <class Out, std::size_t N>
expected<void> encode_impl(wire::writer<Out> &out, const config &cfg, const types::pad_bytes<N> &, std::size_t base_offset) noexcept {
    (void)base_offset;
    return write_pad_bytes(out, N, cfg.pad_byte);
}

template <class Out, std::size_t AlignBits>
expected<void> encode_impl(wire::writer<Out> &out, const config &cfg, const types::pad_to<AlignBits> &, std::size_t base_offset) noexcept {
    return encode_pad_to<AlignBits>(out, cfg, base_offset);
}

template <class Out, int LenBits, int AlignAfterBits>
expected<void> encode_impl(wire::writer<Out> &out, const config &cfg, const types::utf8_string<LenBits, AlignAfterBits> &s,
                           std::size_t base_offset) noexcept {
    if (!is_valid_utf8(s.value)) {
        return unexpected<status_code>(status_code::invalid_utf8);
    }

    constexpr std::uint8_t bom[3] = {0xEFu, 0xBBu, 0xBFu};
    const auto payload_len        = static_cast<std::uint32_t>(3u + s.value.size() + 1u);

    auto st = write_len_field<LenBits>(out, payload_len);
    if (!st) return st;

    st = out.write_bytes(std::span<const std::byte>(reinterpret_cast<const std::byte *>(bom), 3));
    if (!st) return st;

    st = out.write_bytes(std::span<const std::byte>(reinterpret_cast<const std::byte *>(s.value.data()), s.value.size()));
    if (!st) return st;

    st = out.write_byte(std::byte{0x00});
    if (!st) return st;

    if constexpr (AlignAfterBits != 0) {
        return encode_pad_to<static_cast<std::size_t>(AlignAfterBits)>(out, cfg, base_offset);
    }
    return {};
}

template <class Out, int LenBits, int AlignAfterBits>
expected<void> encode_impl(wire::writer<Out> &out, const config &cfg, const types::utf16_string<LenBits, AlignAfterBits> &s,
                           std::size_t base_offset) noexcept {
    const bool little = (cfg.payload_endian == wire::endian::little);
    const std::uint8_t bom[2] = {little ? std::uint8_t{0xFF} : std::uint8_t{0xFE}, little ? std::uint8_t{0xFE} : std::uint8_t{0xFF}};

    const auto payload_len = static_cast<std::uint32_t>(2u + (s.value.size() * 2u) + 2u);
    auto       st          = write_len_field<LenBits>(out, payload_len);
    if (!st) return st;

    st = out.write_bytes(std::span<const std::byte>(reinterpret_cast<const std::byte *>(bom), 2));
    if (!st) return st;

    for (char16_t cu : s.value) {
        st = wire::write_uint(cfg.payload_endian, out, static_cast<std::uint16_t>(cu));
        if (!st) return st;
    }

    // Terminator (0x0000)
    st = wire::write_uint(cfg.payload_endian, out, static_cast<std::uint16_t>(0u));
    if (!st) return st;

    if constexpr (AlignAfterBits != 0) {
        return encode_pad_to<static_cast<std::size_t>(AlignAfterBits)>(out, cfg, base_offset);
    }
    return {};
}

template <class Out, class T, int LenBits, int AlignAfterBits>
expected<void> encode_impl(wire::writer<Out> &out, const config &cfg, const types::dyn_array<T, LenBits, AlignAfterBits> &a,
                           std::size_t base_offset) noexcept {
    static_assert(is_scalar<T>, "dyn_array only supports scalar element types in this POC");

    const std::uint32_t bytes_len = static_cast<std::uint32_t>(a.value.size() * sizeof(std::remove_cvref_t<T>));
    auto                st        = write_len_field<LenBits>(out, bytes_len);
    if (!st) return st;

    for (const auto &e : a.value) {
        st = encode_impl(out, cfg, e, base_offset);
        if (!st) return st;
    }

    if constexpr (AlignAfterBits != 0) {
        return encode_pad_to<static_cast<std::size_t>(AlignAfterBits)>(out, cfg, base_offset);
    }
    return {};
}

template <class Out, class T, std::size_t N, int OptionalLenBits>
expected<void> encode_impl(wire::writer<Out> &out, const config &cfg, const types::fixed_array<T, N, OptionalLenBits> &a,
                           std::size_t base_offset) noexcept {
    static_assert(OptionalLenBits == 0 || is_scalar<T>, "fixed_array optional length only supported for scalar types in this POC");

    if constexpr (OptionalLenBits != 0) {
        const std::uint32_t bytes_len = static_cast<std::uint32_t>(N * sizeof(std::remove_cvref_t<T>));
        auto                st        = write_len_field<OptionalLenBits>(out, bytes_len);
        if (!st) return st;
    }

    for (const auto &e : a.value) {
        auto st = encode_impl(out, cfg, e, base_offset);
        if (!st) return st;
    }
    return {};
}

template <class Out, class Variant, int LenBits, int SelectorBits, int AlignPayloadBits>
expected<void> encode_impl(wire::writer<Out> &out, const config &cfg, const types::union_variant<Variant, LenBits, SelectorBits, AlignPayloadBits> &u,
                           std::size_t base_offset) noexcept {
    static_assert(std::variant_size_v<Variant> >= 1, "Variant must not be empty");
    static_assert(std::is_same_v<std::variant_alternative_t<0, Variant>, std::monostate>, "Variant[0] must be std::monostate");
    static_assert(LenBits == 8 || LenBits == 16 || LenBits == 32, "LenBits must be 8, 16, or 32");
    static_assert(SelectorBits == 8 || SelectorBits == 16 || SelectorBits == 32, "SelectorBits must be 8, 16, or 32");
    static_assert(AlignPayloadBits == 0 || (AlignPayloadBits % 8 == 0), "AlignPayloadBits must be 0 or a multiple of 8");

    const auto selector = static_cast<std::uint32_t>(u.value.index());

    // Compute union payload length (payload+internal padding) without mutating the output.
    const auto meta_bytes = bytes_for_bits<LenBits>() + bytes_for_bits<SelectorBits>();
    sizer      sz{.pos = out.position() + meta_bytes};

    if (selector != 0) {
        auto mst = std::visit(
            [&](const auto &alt) -> expected<void> {
                using Alt = std::remove_cvref_t<decltype(alt)>;
                if constexpr (std::is_same_v<Alt, std::monostate>) {
                    return {};
                } else {
                    return measure_impl(sz, cfg, alt, base_offset);
                }
            },
            u.value);
        if (!mst) {
            return mst;
        }
    }

    if constexpr (AlignPayloadBits != 0) {
        const auto align = static_cast<std::size_t>(AlignPayloadBits / 8);
        const auto off   = base_offset + sz.position();
        sz.advance(pad_needed(off, align));
    }

    const auto payload_len = static_cast<std::uint32_t>(sz.position() - (out.position() + meta_bytes));

    auto st = write_len_field<LenBits>(out, payload_len);
    if (!st) return st;

    st = write_len_field<SelectorBits>(out, selector);
    if (!st) return st;

    if (selector != 0) {
        st = std::visit(
            [&](const auto &alt) -> expected<void> {
                using Alt = std::remove_cvref_t<decltype(alt)>;
                if constexpr (std::is_same_v<Alt, std::monostate>) {
                    return {};
                } else {
                    return encode_impl(out, cfg, alt, base_offset);
                }
            },
            u.value);
        if (!st) return st;
    }

    if constexpr (AlignPayloadBits != 0) {
        st = encode_pad_to<static_cast<std::size_t>(AlignPayloadBits)>(out, cfg, base_offset);
        if (!st) return st;
    }

    return {};
}

template <class Out, class T>
expected<void> encode_impl(wire::writer<Out> &out, const config &cfg, const T &value, std::size_t base_offset) noexcept {
    if constexpr (is_scalar<T>) {
        return encode_scalar(out, cfg, value);
    } else if constexpr (std::is_same_v<std::remove_cvref_t<T>, std::string> || std::is_same_v<std::remove_cvref_t<T>, std::string_view>) {
        return unexpected<status_code>(status_code::error);
    } else if constexpr (std::is_aggregate_v<std::remove_cvref_t<T>> && !is_someip_wrapper<std::remove_cvref_t<T>>) {
        const auto tuple = cbor::tags::to_tuple(value);
        return std::apply(
            [&](const auto &...args) -> expected<void> {
                expected<void> st{};
                auto           one = [&](const auto &arg) {
                    if (!st) {
                        return;
                    }
                    st = encode_impl(out, cfg, arg, base_offset);
                };
                (one(args), ...);
                return st;
            },
            tuple);
    } else {
        return unexpected<status_code>(status_code::error);
    }
}

template <std::size_t N>
expected<void> measure_impl(sizer &s, const config &cfg, const types::pad_bytes<N> &, std::size_t base_offset) noexcept {
    (void)cfg;
    (void)base_offset;
    s.advance(N);
    return {};
}

template <std::size_t AlignBits>
expected<void> measure_impl(sizer &s, const config &cfg, const types::pad_to<AlignBits> &, std::size_t base_offset) noexcept {
    (void)cfg;
    const auto align = AlignBits / 8u;
    const auto off   = base_offset + s.position();
    s.advance(pad_needed(off, align));
    return {};
}

template <int LenBits, int AlignAfterBits>
expected<void> measure_impl(sizer &s, const config &cfg, const types::utf8_string<LenBits, AlignAfterBits> &v, std::size_t base_offset) noexcept {
    (void)cfg;
    if (!is_valid_utf8(v.value)) {
        return unexpected<status_code>(status_code::invalid_utf8);
    }
    const auto payload_len = static_cast<std::size_t>(3u + v.value.size() + 1u);
    s.advance(bytes_for_bits<LenBits>() + payload_len);
    if constexpr (AlignAfterBits != 0) {
        const auto align = static_cast<std::size_t>(AlignAfterBits / 8);
        const auto off   = base_offset + s.position();
        s.advance(pad_needed(off, align));
    }
    return {};
}

template <int LenBits, int AlignAfterBits>
expected<void> measure_impl(sizer &s, const config &cfg, const types::utf16_string<LenBits, AlignAfterBits> &v, std::size_t base_offset) noexcept {
    (void)cfg;
    const auto payload_len = static_cast<std::size_t>(2u + (v.value.size() * 2u) + 2u);
    s.advance(bytes_for_bits<LenBits>() + payload_len);
    if constexpr (AlignAfterBits != 0) {
        const auto align = static_cast<std::size_t>(AlignAfterBits / 8);
        const auto off   = base_offset + s.position();
        s.advance(pad_needed(off, align));
    }
    return {};
}

template <class T, int LenBits, int AlignAfterBits>
expected<void> measure_impl(sizer &s, const config &cfg, const types::dyn_array<T, LenBits, AlignAfterBits> &v, std::size_t base_offset) noexcept {
    (void)cfg;
    static_assert(is_scalar<T>, "dyn_array only supports scalar element types in this POC");
    s.advance(bytes_for_bits<LenBits>() + (v.value.size() * sizeof(std::remove_cvref_t<T>)));
    if constexpr (AlignAfterBits != 0) {
        const auto align = static_cast<std::size_t>(AlignAfterBits / 8);
        const auto off   = base_offset + s.position();
        s.advance(pad_needed(off, align));
    }
    return {};
}

template <class T, std::size_t N, int OptionalLenBits>
expected<void> measure_impl(sizer &s, const config &cfg, const types::fixed_array<T, N, OptionalLenBits> &v, std::size_t base_offset) noexcept {
    (void)cfg;
    if constexpr (OptionalLenBits != 0) {
        s.advance(bytes_for_bits<OptionalLenBits>());
    }
    for (const auto &e : v.value) {
        auto st = measure_impl(s, cfg, e, base_offset);
        if (!st) return st;
    }
    return {};
}

template <class Variant, int LenBits, int SelectorBits, int AlignPayloadBits>
expected<void> measure_impl(sizer &s, const config &cfg, const types::union_variant<Variant, LenBits, SelectorBits, AlignPayloadBits> &u,
                            std::size_t base_offset) noexcept {
    static_assert(std::variant_size_v<Variant> >= 1, "Variant must not be empty");
    static_assert(std::is_same_v<std::variant_alternative_t<0, Variant>, std::monostate>, "Variant[0] must be std::monostate");
    static_assert(LenBits == 8 || LenBits == 16 || LenBits == 32, "LenBits must be 8, 16, or 32");
    static_assert(SelectorBits == 8 || SelectorBits == 16 || SelectorBits == 32, "SelectorBits must be 8, 16, or 32");
    static_assert(AlignPayloadBits == 0 || (AlignPayloadBits % 8 == 0), "AlignPayloadBits must be 0 or a multiple of 8");

    s.advance(bytes_for_bits<LenBits>() + bytes_for_bits<SelectorBits>());

    const auto selector = u.value.index();
    if (selector != 0) {
        auto st = std::visit(
            [&](const auto &alt) -> expected<void> {
                using Alt = std::remove_cvref_t<decltype(alt)>;
                if constexpr (std::is_same_v<Alt, std::monostate>) {
                    return {};
                } else {
                    return measure_impl(s, cfg, alt, base_offset);
                }
            },
            u.value);
        if (!st) return st;
    }

    if constexpr (AlignPayloadBits != 0) {
        const auto align = static_cast<std::size_t>(AlignPayloadBits / 8);
        const auto off   = base_offset + s.position();
        s.advance(pad_needed(off, align));
    }

    return {};
}

template <class T>
expected<void> measure_impl(sizer &s, const config &cfg, const T &value, std::size_t base_offset) noexcept {
    if constexpr (is_byte<T> || is_bool<T>) {
        (void)cfg;
        (void)value;
        s.advance(1);
        return {};
    } else if constexpr (is_integral_not_bool<T> || is_enum<T>) {
        (void)cfg;
        (void)value;
        s.advance(sizeof(std::remove_cvref_t<T>));
        return {};
    } else if constexpr (is_float32<T>) {
        (void)cfg;
        (void)value;
        s.advance(4);
        return {};
    } else if constexpr (is_float64<T>) {
        (void)cfg;
        (void)value;
        s.advance(8);
        return {};
    } else if constexpr (std::is_aggregate_v<std::remove_cvref_t<T>> && !is_someip_wrapper<std::remove_cvref_t<T>>) {
        const auto tuple = cbor::tags::to_tuple(value);
        return std::apply(
            [&](const auto &...args) -> expected<void> {
                expected<void> st{};
                auto           one = [&](const auto &arg) {
                    if (!st) {
                        return;
                    }
                    st = measure_impl(s, cfg, arg, base_offset);
                };
                (one(args), ...);
                return st;
            },
            tuple);
    } else {
        return unexpected<status_code>(status_code::error);
    }
}

} // namespace someip::ser::detail

namespace someip::ser {

template <class T>
expected<std::size_t> measure(const config &cfg, const T &value, std::size_t base_offset = 0) noexcept {
    detail::sizer s{};
    auto          st = detail::measure_impl(s, cfg, value, base_offset);
    if (!st) {
        return unexpected<status_code>(st.error());
    }
    return s.position();
}

template <class Out, class T>
expected<void> encode(Out &out, const config &cfg, const T &value, std::size_t base_offset = 0) noexcept {
    wire::writer<Out> w{out};
    return detail::encode_impl(w, cfg, value, base_offset);
}

} // namespace someip::ser
