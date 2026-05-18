#pragma once

namespace cbor::tags::detail {

enum class cddl_shared_pointer_mode { nullable, shared_graph };

template <typename T> struct cddl_scope_traits {};
template <typename T> struct cddl_tagged_bstr_array_traits {};
template <typename T> struct cddl_homogeneous_array_traits {};
template <typename T> struct cddl_multi_dimensional_array_traits {};

} // namespace cbor::tags::detail
