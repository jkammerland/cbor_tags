
#include "cbor_tags/cbor_decoder.h"
#include "test_util.h"

#include <deque>
#include <doctest/doctest.h>
#include <doctest/parts/doctest_fwd.h>
#include <fmt/core.h>
#include <list>
#include <memory_resource>
#include <nameof.hpp>

using namespace cbor::tags;

TEST_CASE_TEMPLATE("CBOR Decoder", T, std::vector<std::byte>, std::deque<std::byte>) { auto [data, in] = make_data_and_decoder<T>(); }