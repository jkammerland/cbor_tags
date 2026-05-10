#pragma once

#include "cbor_tags/cbor.h"

namespace cbor::tags {

template <typename Self> struct cbor_codec_mixin_base {
    constexpr void        encode() = delete;
    constexpr status_code decode() = delete;
};

} // namespace cbor::tags
