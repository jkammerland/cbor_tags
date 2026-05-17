#pragma once

namespace cbor::tags::detail {

template <typename T> struct cddl_tagged_bstr_array_traits {};
template <typename T> struct cddl_homogeneous_array_traits {};
template <typename T> struct cddl_multi_dimensional_array_traits {};

} // namespace cbor::tags::detail
