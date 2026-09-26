#pragma once

#include <doctest/doctest.h>
#include <version>

#if defined(__cpp_lib_indirect) && __cpp_lib_indirect >= 202502L && defined(__cpp_lib_polymorphic) && __cpp_lib_polymorphic >= 202502L
#include "../examples/cxx26_value_wrappers.h"

#include <cbor_tags/extensions/cbor_visualization.h>
#include <cbor_tags/extensions/smart_ptr.h>
#include <cbor_tags/extensions/std_expected.h>
#include <cstddef>
#include <expected>
#include <list>
#include <memory>
#include <memory_resource>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace cbor::tags;
using cbor::tags::ext::std_indirect::std_indirect_codec;
using namespace cbor_value_example;

namespace cbor_value_test {

struct record {
    int         id{};
    std::string name;
};

struct observing_resource : std::pmr::memory_resource {
    bool        fail{};
    std::size_t allocations{};
    void       *do_allocate(std::size_t bytes, std::size_t alignment) override {
        if (fail) {
            throw std::bad_alloc{};
        }
        ++allocations;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }
    void do_deallocate(void *pointer, std::size_t bytes, std::size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }
    bool do_is_equal(const memory_resource &other) const noexcept override { return this == &other; }
};

struct immovable_value {
    int value{};
    immovable_value()                                                 = default;
    immovable_value(const immovable_value &)                          = delete;
    immovable_value(immovable_value &&)                               = delete;
    immovable_value               &operator=(const immovable_value &) = delete;
    immovable_value               &operator=(immovable_value &&)      = delete;
    template <typename Coder> auto decode(Coder &coder) { return coder(value); }
    template <typename Coder> auto encode(Coder &coder) const { return coder(value); }
};

static_assert(std::default_initializable<immovable_value>);
static_assert(!std::move_constructible<immovable_value>);

struct throwing_value {
    static inline bool fail{};
    int                value{};
    throwing_value() {
        if (fail) {
            throw std::runtime_error("construction failed");
        }
    }
    template <typename Coder> auto transcode(Coder &coder) { return coder(value); }
    template <typename Coder> auto encode(Coder &coder) const { return coder(value); }
};

} // namespace cbor_value_test

#endif
