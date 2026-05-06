#include <cbor_tags/cbor_decoder.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

static bool fail_allocations = false;

void *operator new(std::size_t size) {
    if (fail_allocations) {
        throw std::bad_alloc{};
    }
    if (void *ptr = std::malloc(size)) {
        return ptr;
    }
    throw std::bad_alloc{};
}

void operator delete(void *ptr) noexcept { std::free(ptr); }
void operator delete(void *ptr, std::size_t) noexcept { std::free(ptr); }

int main() {
    std::vector<std::byte> buffer{std::byte{0xD8}, std::byte{0x64}, std::byte{0x01}};
    auto                   view = cbor::tags::find_tags<100>(buffer);

    fail_allocations = true;
    try {
        auto it = view.begin();
        (void)it;
    } catch (const std::bad_alloc &) {
        std::puts("PROOF: lazy scanner allocates during begin() and lets std::bad_alloc escape");
        return 0;
    }

    std::puts("expected std::bad_alloc was not observed");
    return 1;
}
