#pragma once

#if __has_include("cbor_tags/cbor_tags_config.h")
#include "cbor_tags/cbor_tags_config.h"
#endif

#ifndef CBOR_TAGS_STL_ONLY
#define CBOR_TAGS_STL_ONLY 0
#endif
#ifndef CBOR_TAGS_USE_STD_EXPECTED
#define CBOR_TAGS_USE_STD_EXPECTED CBOR_TAGS_STL_ONLY
#endif

#if CBOR_TAGS_USE_STD_EXPECTED
#include <expected>
#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202202L
#error "CBOR_TAGS_USE_STD_EXPECTED requires C++23 <expected> with __cpp_lib_expected >= 202202L"
#endif
#else
#include "cbor_tags/detail/expected.h"
#endif
#include <utility>

namespace cbor::tags {
using std::in_place;
using std::in_place_t;
#if CBOR_TAGS_USE_STD_EXPECTED
using std::bad_expected_access;
using std::expected;
using std::unexpect;
using std::unexpect_t;
using std::unexpected;
#else
using detail::expected_impl::bad_expected_access;
using detail::expected_impl::expected;
using detail::expected_impl::unexpect;
using detail::expected_impl::unexpect_t;
using detail::expected_impl::unexpected;
#endif
} // namespace cbor::tags
