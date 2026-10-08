#pragma once

#include <algorithm>
#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/codec/shared_ptr.h>
#include <cbor_tags/codec/unique_ptr.h>
#include <cbor_tags/extensions/cbor_visualization.h>
#include <doctest/doctest.h>
#include <list>
#include <map>
#include <ostream>
#include <ranges>
#include <set>
#include <unordered_set>
#include <variant>
#include <vector>
#include <version>

#if __has_include(<boost/container/slist.hpp>)
#include <boost/array.hpp>
#include <boost/container/deque.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <boost/container/list.hpp>
#include <boost/container/map.hpp>
#include <boost/container/set.hpp>
#include <boost/container/slist.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/container/stable_vector.hpp>
#include <boost/container/static_vector.hpp>
#include <boost/container/string.hpp>
#include <boost/container/vector.hpp>
#include <boost/move/unique_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/unordered/unordered_map.hpp>
#include <boost/unordered/unordered_set.hpp>
#define CBOR_TAGS_TEST_BOOST_CONTAINERS 1
#endif

#if __has_include(<boost/container/devector.hpp>)
#include <boost/container/devector.hpp>
#define CBOR_TAGS_TEST_BOOST_DEVECTOR 1
#endif

#if __has_include(<boost/unordered/unordered_flat_map.hpp>) && __has_include(<boost/unordered/unordered_flat_set.hpp>) && \
    __has_include(<boost/unordered/unordered_node_map.hpp>) && __has_include(<boost/unordered/unordered_node_set.hpp>)
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#include <boost/unordered/unordered_node_map.hpp>
#include <boost/unordered/unordered_node_set.hpp>
#define CBOR_TAGS_TEST_BOOST_OPEN_UNORDERED 1
#endif

#if __has_include(<boost/container/hub.hpp>) && __has_include(<boost/container/segtor.hpp>)
#include <boost/container/hub.hpp>
#include <boost/container/segtor.hpp>
#define CBOR_TAGS_TEST_BOOST_HUB_SEGTOR 1
#endif

#if defined(__cpp_lib_flat_set) && __cpp_lib_flat_set >= 202207L
#include <flat_set>
#endif
#if defined(__cpp_lib_flat_map) && __cpp_lib_flat_map >= 202207L
#include <flat_map>
#endif
#if defined(__cpp_lib_inplace_vector) && __cpp_lib_inplace_vector >= 202406L
#include <inplace_vector>
#endif
#if defined(__cpp_lib_hive) && __cpp_lib_hive >= 202502L
#include <hive>
#endif

using namespace cbor::tags;

namespace cbor_container_test {

template <typename Range> std::vector<int> sorted_values(const Range &range) {
    std::vector<int> result(range.begin(), range.end());
    std::ranges::sort(result);
    return result;
}

} // namespace cbor_container_test
