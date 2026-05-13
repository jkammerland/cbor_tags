#include <cbor_tags/cbor_reflection.h>
#include <cbor_tags/cbor_reflection_config.h>
#include <cbor_tags/cbor_reflection_count.h>
#include <cbor_tags/cbor_reflection_named.h>
#if CBOR_TAGS_HAS_BOOST_PFR_NAMES
#include <cbor_tags/cbor_reflection_pfr.h>
#endif
#include <cbor_tags/cbor_reflection_std.h>
#include <doctest/doctest.h>

TEST_CASE("reflection split headers are directly includable") { CHECK(true); }
