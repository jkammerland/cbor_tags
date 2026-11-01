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
