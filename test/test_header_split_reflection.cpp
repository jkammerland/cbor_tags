// Keep the umbrella header last so this test catches split-header dependencies.
// clang-format off
#include <cbor_tags/cbor_reflection_config.h>
#include <cbor_tags/cbor_reflection_count.h>
#include <cbor_tags/cbor_reflection_named.h>
#include <cbor_tags/cbor_reflection_pfr.h>
#include <cbor_tags/cbor_reflection_std.h>
#include <cbor_tags/cbor_reflection.h>
// clang-format on
#include <doctest/doctest.h>

TEST_CASE("reflection split headers are directly includable") { CHECK(true); }
