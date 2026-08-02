#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

namespace {

bool fail_next_global_allocation = false;

struct tracked_value {
    std::uint64_t value{};

    static inline int destructor_calls{};
    static inline int delete_calls{};

    ~tracked_value() { ++destructor_calls; }

    static void *operator new(std::size_t size) {
        auto *storage = std::malloc(size);
        if (storage == nullptr) {
            throw std::bad_alloc{};
        }
        fail_next_global_allocation = true;
        return storage;
    }

    static void operator delete(void *) noexcept {
        // Keep the storage valid so a duplicate destruction is observable
        // without relying on allocator-specific double-free behavior.
        ++delete_calls;
    }
};

template <typename T> class structural_shared_pointer {
  public:
    using element_type = T;

    structural_shared_pointer()                                                 = default;
    structural_shared_pointer(const structural_shared_pointer &)                = default;
    structural_shared_pointer(structural_shared_pointer &&) noexcept            = default;
    structural_shared_pointer &operator=(const structural_shared_pointer &)     = default;
    structural_shared_pointer &operator=(structural_shared_pointer &&) noexcept = default;

    [[nodiscard]] T *get() const noexcept { return pointer_.get(); }
    [[nodiscard]] T &operator*() const noexcept { return *pointer_; }
    explicit         operator bool() const noexcept { return static_cast<bool>(pointer_); }

    void reset() noexcept { pointer_.reset(); }
    void reset(T *raw) { pointer_.reset(raw); }

  private:
    std::shared_ptr<T> pointer_;
};

} // namespace

void *operator new(std::size_t size) {
    if (fail_next_global_allocation) {
        fail_next_global_allocation = false;
        throw std::bad_alloc{};
    }
    if (auto *storage = std::malloc(size)) {
        return storage;
    }
    throw std::bad_alloc{};
}

void operator delete(void *storage) noexcept { std::free(storage); }
void operator delete(void *storage, std::size_t) noexcept { std::free(storage); }

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::smart_ptr;

    static_assert(IsSharedPointer<structural_shared_pointer<tracked_value>>);
    static_assert(!std::constructible_from<structural_shared_pointer<tracked_value>, std::shared_ptr<tracked_value>>);

    const std::vector<std::byte>             encoded{std::byte{0xD8}, std::byte{0x1C}, std::byte{0x01}};
    structural_shared_pointer<tracked_value> decoded;
    auto                                     dec    = make_decoder<shared_ptr_codec>(encoded);
    const auto                               result = dec(decoded);

    const auto allocation_failed = !result && result.error() == status_code::out_of_memory;
    return allocation_failed && !decoded && tracked_value::destructor_calls == 1 && tracked_value::delete_calls == 1 ? 0 : 1;
}
