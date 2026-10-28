#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <list>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

using namespace cbor::tags;

using input_storage      = std::list<std::span<const std::byte>>;
using output_storage     = std::list<std::span<std::byte>>;
using input_source_type  = decltype(make_input_source(std::declval<input_storage &>()));
using output_source_type = decltype(make_output_source(std::declval<output_storage &>()));
using decode_operation   = decltype(make_resumable_decoder(std::declval<input_source_type &>())(std::declval<int &>()));
using encode_operation   = decltype(make_resumable_encoder(std::declval<output_source_type &>())(std::declval<const int &>()));

static_assert(!std::is_copy_constructible_v<resumable<status_code>>);
static_assert(!std::is_copy_assignable_v<resumable<status_code>>);
static_assert(std::is_nothrow_move_constructible_v<resumable<status_code>>);
static_assert(!std::is_copy_constructible_v<decode_operation>);
static_assert(!std::is_copy_constructible_v<encode_operation>);
static_assert(std::is_move_constructible_v<decode_operation>);
static_assert(std::is_move_constructible_v<encode_operation>);
static_assert(std::is_same_v<decltype(std::declval<const decode_operation &>().result()), expected<std::size_t, status_code>>);

int main() {
#ifdef CBOR_RESUMABLE_UNBOUNDED_ZERO_INPUT
    struct empty {};
    input_storage      storage;
    auto               source = make_input_source(storage);
    std::vector<empty> destination;
    auto               operation = make_resumable_decoder(source)(destination);
    return operation.done() ? 0 : 1;
#else
    resumable<status_code> ready{status_code::success};
    auto                   awaiter = std::move(ready).operator co_await();
    return awaiter.await_ready() && awaiter.await_resume() == status_code::success ? 0 : 1;
#endif
}
