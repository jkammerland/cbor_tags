#pragma once

#include <cbor_tags/detail/smart_ptr_decode.h>

namespace smart_ptr_scope_ref_test {

// Defined in the split-header TU so both include paths instantiate every callback.
cbor::tags::smart_ptr::detail::encode_scope_ref make_encode_ref(cbor::tags::smart_ptr::shared_ptr_encode_scope &scope);
cbor::tags::smart_ptr::detail::decode_scope_ref make_decode_ref(cbor::tags::smart_ptr::shared_ptr_decode_scope &scope);

} // namespace smart_ptr_scope_ref_test
