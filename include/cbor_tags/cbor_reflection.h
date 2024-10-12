#pragma once
namespace cbor::tags {

template <class T> constexpr auto to_tuple(T &&object) noexcept;

} // namespace cbor::tags

#include "cbor_tags/cbor_reflection_impl.h"