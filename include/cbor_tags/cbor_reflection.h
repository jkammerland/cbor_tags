#pragma once

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_reflection_config.h"

#include <tuple>
#include <type_traits>
#include <utility>

namespace cbor::tags {

#if !CBOR_TAGS_HAS_STD_REFLECTION && !CBOR_TAGS_HAS_BOOST_PFR_NAMES

template <class T> constexpr auto to_tuple(T &&object) noexcept;

#endif

} // namespace cbor::tags

#if CBOR_TAGS_HAS_STD_REFLECTION
#include "cbor_tags/cbor_reflection_std.h"
#elif CBOR_TAGS_HAS_BOOST_PFR_NAMES
#include "cbor_tags/cbor_reflection_pfr.h"
#else
#include "cbor_tags/cbor_reflection_impl.h"
#endif
