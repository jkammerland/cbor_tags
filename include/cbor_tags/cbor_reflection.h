#pragma once

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_reflection_config.h"

#include <tuple>
#include <type_traits>
#include <utility>

namespace cbor::tags {

#if !CBOR_TAGS_HAS_STD_REFLECTION

template <class T> constexpr auto to_tuple(T &&object) noexcept;

#endif

} // namespace cbor::tags

#if CBOR_TAGS_HAS_STD_REFLECTION
#include "cbor_tags/cbor_reflection_std.h"
#else
#include "cbor_tags/cbor_reflection_impl.h"
#endif
