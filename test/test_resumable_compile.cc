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

namespace hook_contract {

struct context {};

// Member lookup must retain Access friendship and each hook's cv/ref contract.
template <typename Result> class member_hooks {
    friend struct cbor::tags::Access;

    Result resumable_encode(context &) const &;
    Result resumable_decode(context &) &;
    Result resumable_transcode(context &) &;
};

using valid_member_hooks = member_hooks<resumable<status_code>>;
static_assert(detail::HasResumableEncodeMethod<context, const valid_member_hooks>);
static_assert(detail::HasResumableDecodeMethod<context, valid_member_hooks>);
static_assert(detail::HasResumableTranscodeMethod<context, valid_member_hooks>);
static_assert(!detail::HasResumableDecodeMethod<context, const valid_member_hooks>);
static_assert(!detail::HasResumableTranscodeMethod<context, const valid_member_hooks>);
static_assert(!detail::HasResumableEncodeMethod<context, member_hooks<status_code>>);
static_assert(!detail::HasResumableDecodeMethod<context, member_hooks<status_code>>);
static_assert(!detail::HasResumableTranscodeMethod<context, member_hooks<status_code>>);

struct mutable_encode_member {
    resumable<status_code> resumable_encode(context &) &;
};
static_assert(!detail::HasResumableEncodeMethod<context, mutable_encode_member>);

// ADL and mixin results must be the exact resumable type, not merely convertible.
template <typename Result> struct free_hooks {};
template <typename Result> Result resumable_encode(context &, const free_hooks<Result> &);
template <typename Result> Result resumable_decode(context &, free_hooks<Result> &);
template <typename Result> Result resumable_transcode(context &, free_hooks<Result> &);

using valid_free_hooks = free_hooks<resumable<status_code>>;
static_assert(detail::HasResumableEncodeFreeFunction<context, const valid_free_hooks>);
static_assert(detail::HasResumableDecodeFreeFunction<context, valid_free_hooks>);
static_assert(detail::HasResumableTranscodeFreeFunction<context, valid_free_hooks>);
static_assert(!detail::HasResumableDecodeFreeFunction<context, const valid_free_hooks>);
static_assert(!detail::HasResumableTranscodeFreeFunction<context, const valid_free_hooks>);
static_assert(!detail::HasResumableEncodeFreeFunction<context, free_hooks<status_code>>);
static_assert(!detail::HasResumableDecodeFreeFunction<context, free_hooks<status_code>>);
static_assert(!detail::HasResumableTranscodeFreeFunction<context, free_hooks<status_code>>);
static_assert(!detail::HasResumableEncodeFreeFunction<context, free_hooks<resumable<status_code> &>>);

template <typename Result> struct mixin_hooks {
    Result resumable_encode(const int &) &;
    Result resumable_decode(int &) &;
    Result resumable_transcode(int &) &;
};

using valid_mixin_hooks = mixin_hooks<resumable<status_code>>;
static_assert(detail::HasResumableEncodeMixin<valid_mixin_hooks, const int>);
static_assert(detail::HasResumableDecodeMixin<valid_mixin_hooks, int>);
static_assert(detail::HasResumableTranscodeMixin<valid_mixin_hooks, int>);
static_assert(!detail::HasResumableDecodeMixin<valid_mixin_hooks, const int>);
static_assert(!detail::HasResumableTranscodeMixin<valid_mixin_hooks, const int>);
static_assert(!detail::HasResumableEncodeMixin<mixin_hooks<status_code>, int>);
static_assert(!detail::HasResumableDecodeMixin<mixin_hooks<status_code>, int>);
static_assert(!detail::HasResumableTranscodeMixin<mixin_hooks<status_code>, int>);
static_assert(!detail::HasResumableEncodeMixin<mixin_hooks<resumable<status_code> &>, int>);

} // namespace hook_contract

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
