#pragma once

namespace cbor::tags::cddl {

enum class cddl_shared_pointer_mode { nullable, shared_graph };

template <typename T> struct cddl_scope_traits {};
template <typename T> struct cddl_tagged_bstr_array_traits {};
template <typename T> struct cddl_homogeneous_array_traits {};
template <typename T> struct cddl_multi_dimensional_array_traits {};

} // namespace cbor::tags::cddl

namespace cbor::tags::detail {

using cddl::cddl_homogeneous_array_traits;
using cddl::cddl_multi_dimensional_array_traits;
using cddl::cddl_scope_traits;
using cddl::cddl_shared_pointer_mode;
using cddl::cddl_tagged_bstr_array_traits;

} // namespace cbor::tags::detail
