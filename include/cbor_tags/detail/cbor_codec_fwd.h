#pragma once

#include "cbor_tags/cbor_concepts.h"

namespace cbor::tags {

template <typename OutputBuffer, IsOptions Options, template <typename> typename... Encoders>
    requires CborOutputBuffer<OutputBuffer>
struct encoder;

template <typename InputBuffer, IsOptions Options, template <typename> typename... Decoders>
    requires CborInputBuffer<InputBuffer>
struct decoder;

// The mixins every decoder and encoder is made of. Public construction
// supports composition of codec mixins.
// NOLINTNEXTLINE(bugprone-crtp-constructor-accessibility)
template <typename T> struct cbor_header_decoder;
// NOLINTNEXTLINE(bugprone-crtp-constructor-accessibility)
template <typename T> struct cbor_indefinite_decoder;

// NOLINTNEXTLINE(bugprone-crtp-constructor-accessibility)
template <typename T> struct cbor_header_encoder;
// NOLINTNEXTLINE(bugprone-crtp-constructor-accessibility)
template <typename T> struct cbor_indefinite_encoder;
// NOLINTNEXTLINE(bugprone-crtp-constructor-accessibility)
template <typename T> struct cbor_optional_encoder;
// NOLINTNEXTLINE(bugprone-crtp-constructor-accessibility)
template <typename T> struct cbor_variant_encoder;

} // namespace cbor::tags

namespace cbor::tags::detail {

// The two mixins every decoder is made of. Anything else in its pack is a codec
template <template <typename> typename Mixin> inline constexpr bool is_core_decoder_mixin                          = false;
template <> inline constexpr bool                                   is_core_decoder_mixin<cbor_header_decoder>     = true;
template <> inline constexpr bool                                   is_core_decoder_mixin<cbor_indefinite_decoder> = true;

template <typename Source, typename Options, template <typename> typename... Codecs> class resumable_decode_context;
template <typename Source, typename Options, template <typename> typename... Codecs> class resumable_encode_context;

template <typename T> inline constexpr bool is_resumable_context = false;
template <typename Source, typename Options, template <typename> typename... Codecs>
inline constexpr bool is_resumable_context<resumable_decode_context<Source, Options, Codecs...>> = true;
template <typename Source, typename Options, template <typename> typename... Codecs>
inline constexpr bool is_resumable_context<resumable_encode_context<Source, Options, Codecs...>> = true;

} // namespace cbor::tags::detail
