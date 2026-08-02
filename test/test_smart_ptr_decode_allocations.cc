#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>

namespace {

std::atomic_bool         track_allocations{};
std::atomic<std::size_t> allocation_count{};

} // namespace

void *operator new(std::size_t size) {
    if (track_allocations.load(std::memory_order_relaxed)) {
        allocation_count.fetch_add(1U, std::memory_order_relaxed);
    }
    if (void *pointer = std::malloc(size)) {
        return pointer;
    }
    throw std::bad_alloc{};
}

void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept { std::free(pointer); }

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::smart_ptr;

    const std::array bytes{
        std::byte{0xD8}, std::byte{0x1C}, std::byte{0x01}, std::byte{0xD8}, std::byte{0x1D}, std::byte{0x00},
    };

    shared_ptr_decode_scope table;
    table.reserve(1U);
    auto dec = make_decoder<shared_ptr_codec>(bytes);
    dec.set_shared_ptr_scope(table);

    std::shared_ptr<std::uint64_t> first;
    allocation_count.store(0U, std::memory_order_relaxed);
    track_allocations.store(true, std::memory_order_relaxed);
    const auto first_result = dec(first);
    track_allocations.store(false, std::memory_order_relaxed);
    if (!first_result || !first || *first != 1U || allocation_count.load(std::memory_order_relaxed) != 1U) {
        return 1;
    }

    std::shared_ptr<std::uint64_t> second;
    allocation_count.store(0U, std::memory_order_relaxed);
    track_allocations.store(true, std::memory_order_relaxed);
    const auto second_result = dec(second);
    track_allocations.store(false, std::memory_order_relaxed);
    if (!second_result || first != second || allocation_count.load(std::memory_order_relaxed) != 0U) {
        return 2;
    }
    return 0;
}
