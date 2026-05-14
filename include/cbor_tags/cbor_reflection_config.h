#pragma once

#if defined(__cpp_impl_reflection) && __cpp_impl_reflection >= 202506L
#include <meta>
#define CBOR_TAGS_HAS_STD_REFLECTION 1
#else
#define CBOR_TAGS_HAS_STD_REFLECTION 0
#endif

#ifndef CBOR_TAGS_USE_BOOST_PFR_NAMES
#define CBOR_TAGS_USE_BOOST_PFR_NAMES 0
#endif

#if CBOR_TAGS_USE_BOOST_PFR_NAMES
#if !__has_include(<boost/pfr/core_name.hpp>)
#error "CBOR_TAGS_USE_BOOST_PFR_NAMES requires Boost.PFR with <boost/pfr/core_name.hpp> (Boost 1.84 or newer)"
#endif
#include <boost/pfr/core.hpp>
#include <boost/pfr/core_name.hpp>
#if !defined(BOOST_PFR_CORE_NAME_ENABLED) || !BOOST_PFR_CORE_NAME_ENABLED
#error "CBOR_TAGS_USE_BOOST_PFR_NAMES requires BOOST_PFR_CORE_NAME_ENABLED"
#endif
#define CBOR_TAGS_HAS_BOOST_PFR_NAMES 1
#else
#define CBOR_TAGS_HAS_BOOST_PFR_NAMES 0
#endif

#if CBOR_TAGS_HAS_STD_REFLECTION || CBOR_TAGS_HAS_BOOST_PFR_NAMES
#define CBOR_TAGS_HAS_NAMED_REFLECTION 1
#else
#define CBOR_TAGS_HAS_NAMED_REFLECTION 0
#endif
