#pragma once

#include "cbor_tags/cbor.h"

namespace cbor::tags::detail {

struct encode_status_exception {
    status_code status;
};

template <typename Result> constexpr void throw_on_encode_error(Result &&result) {
    if (!result.has_value()) {
        throw encode_status_exception{result.error()};
    }
}

} // namespace cbor::tags::detail
