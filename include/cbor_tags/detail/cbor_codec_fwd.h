#pragma once

#include "cbor_tags/cbor_concepts.h"

namespace cbor::tags {

template <typename OutputBuffer, IsOptions Options, template <typename> typename... Encoders>
    requires CborOutputBuffer<OutputBuffer>
struct encoder;

template <typename InputBuffer, IsOptions Options, template <typename> typename... Decoders>
    requires CborInputBuffer<InputBuffer>
struct decoder;

} // namespace cbor::tags

namespace cbor::tags::detail {

template <typename Source, typename Options, template <typename> typename... Codecs> class resumable_decode_context;
template <typename Source, typename Options, template <typename> typename... Codecs> class resumable_encode_context;

template <typename T> inline constexpr bool is_resumable_context = false;
template <typename Source, typename Options, template <typename> typename... Codecs>
inline constexpr bool is_resumable_context<resumable_decode_context<Source, Options, Codecs...>> = true;
template <typename Source, typename Options, template <typename> typename... Codecs>
inline constexpr bool is_resumable_context<resumable_encode_context<Source, Options, Codecs...>> = true;

} // namespace cbor::tags::detail
