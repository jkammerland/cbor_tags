#pragma once

namespace cbor::tags::detail {

enum class cddl_shared_pointer_mode { nullable, shared_graph };

} // namespace cbor::tags::detail

namespace cbor::tags::cddl {

template <typename T> struct tagged_bstr_array_traits {};
template <typename T> struct homogeneous_array_traits {};
template <typename T> struct multi_dimensional_array_traits {};
// The selected application codec serializes T exactly as this complete wire type.
template <typename T> struct wire_type {};

template <typename T> using wire_type_t = typename wire_type<T>::type;

} // namespace cbor::tags::cddl

namespace cbor::tags::detail {

using cddl::homogeneous_array_traits;
using cddl::multi_dimensional_array_traits;
using cddl::tagged_bstr_array_traits;

} // namespace cbor::tags::detail
