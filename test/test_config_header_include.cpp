#include "cbor_tags/cbor_tags_config.h"

#include <doctest/doctest.h>

using namespace cbor::tags;

TEST_CASE("config header should be self-contained") {
    debug::print("noop");
    debug::println("noop");
    CHECK(true);
}
