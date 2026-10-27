#include <array>
#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <span>

namespace {
int allocations_until_failure = -1;
}

void *operator new(std::size_t size) {
    if (allocations_until_failure > 0 && --allocations_until_failure == 0) {
        allocations_until_failure = -1;
        throw std::bad_alloc{};
    }
    if (void *storage = std::malloc(size == 0U ? 1U : size))
        return storage;
    throw std::bad_alloc{};
}

void operator delete(void *storage) noexcept { std::free(storage); }
void operator delete(void *storage, std::size_t) noexcept { std::free(storage); }

int main() {
    using namespace cbor::tags;
    const std::array<std::byte, 3> input{std::byte{0x18}, std::byte{0x2a}, std::byte{0}};
    const std::array               chunks{std::span{input}.subspan(0, 1), std::span{input}.subspan(1, 1), std::span{input}.subspan(2, 1)};
    bool                           saw_one_accepted = false;
    bool                           saw_two_accepted = false;

    // Inject failures during descriptor normalization. The parser must not run
    // until the entire batch is admitted.
    for (int fail_at = 1; fail_at <= 32; ++fail_at) {
        int  value                = -1;
        auto operation            = make_resumable_decoder()(value);
        allocations_until_failure = fail_at;
        const auto progress       = operation(chunks);
        allocations_until_failure = -1;
        if (progress.accepted_chunks != 1U && progress.accepted_chunks != 2U)
            continue;
        if (progress.state != resumable_state::failed || progress.error != status_code::out_of_memory || progress.bytes != 0U ||
            value != -1)
            return 1;
        if (operation.result() || operation.result().error() != status_code::out_of_memory)
            return 2;
        const auto remaining = operation.remaining_input();
        if (remaining.size() != progress.accepted_chunks)
            return 3;
        for (std::size_t index = 0; index < remaining.size(); ++index) {
            const auto span = remaining[index].try_span();
            if (!span || span->size() != 1U || (*span)[0] != input[index])
                return 4;
        }
        saw_one_accepted |= progress.accepted_chunks == 1U;
        saw_two_accepted |= progress.accepted_chunks == 2U;
    }
    if (!saw_one_accepted || !saw_two_accepted)
        return 5;

    // If publishing a generator prefix cannot allocate its cursor, the bytes
    // already written remain recoverable from the retained output descriptor.
    bool saw_written_prefix_failure = false;
    for (int fail_at = 1; fail_at <= 32; ++fail_at) {
        auto                     operation = make_resumable_encoder().chunks(1);
        std::array<std::byte, 1> buffer{};
        allocations_until_failure = fail_at;
        const auto progress       = operation(buffer);
        allocations_until_failure = -1;
        if (progress.state != resumable_state::failed || progress.error != status_code::out_of_memory || progress.bytes != 1U)
            continue;
        const auto remaining = operation.remaining_output();
        if (remaining.size() != 1U)
            return 6;
        const auto prefix = remaining[0].produced().try_span();
        if (!prefix || prefix->size() != 1U || (*prefix)[0] != std::byte{1} || buffer[0] != std::byte{1})
            return 7;
        saw_written_prefix_failure = true;
    }
    return saw_written_prefix_failure ? 0 : 8;
}
