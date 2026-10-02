#pragma once

// Generated into a benchmark-only include directory. All translation units in
// the tl comparison executable use these aliases, including the codec headers.
#include <tl/expected.hpp>
#include <utility>

namespace cbor::tags {
using std::in_place;
using std::in_place_t;
using tl::bad_expected_access;
using tl::expected;
using tl::unexpect;
using tl::unexpect_t;
using tl::unexpected;
} // namespace cbor::tags
