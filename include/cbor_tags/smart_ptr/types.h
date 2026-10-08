#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/codec.h"
#include "cbor_tags/detail/cbor_encode_error.h"
#include "cbor_tags/detail/smart_ptr_traits.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cbor::tags::smart_ptr {

template <typename T>
concept IsSmartPointer = detail::SmartPointer<T>;

template <typename T>
concept IsUniquePointer = detail::UniquePointer<T>;

template <typename T>
concept IsSharedPointer = detail::SharedPointer<T>;

enum class shared_ptr_entry_state : std::uint8_t { encoding, complete };
enum class shared_ptr_observation_kind : std::uint8_t { first, reference };

struct shared_ptr_observation {
    shared_ptr_observation_kind kind{};
    std::size_t                 index{};
};

struct shared_ptr_encode_key {
    const void *target{};
    const void *pointer_type{};

    bool operator==(const shared_ptr_encode_key &) const = default;
};

struct shared_ptr_decode_entry {
    std::shared_ptr<void>  pointer{};
    const void            *pointer_type{};
    shared_ptr_entry_state state{shared_ptr_entry_state::encoding};
};

template <typename Scope>
concept SharedPtrEncodeScope = requires(Scope &scope, const shared_ptr_encode_key &key, std::size_t index) {
    { scope.observe(key) } -> std::same_as<expected<shared_ptr_observation, status_code>>;
    { scope.observe_untracked() } -> std::same_as<expected<void, status_code>>;
    { scope.mark_complete(index) } -> std::same_as<void>;
    { scope.reset() } -> std::same_as<void>;
};

template <typename Scope>
concept SharedPtrDecodeScope = requires(Scope &scope, const shared_ptr_decode_entry &entry, std::size_t index) {
    { scope.insert(entry) } -> std::same_as<expected<std::size_t, status_code>>;
    { scope.insert_untracked() } -> std::same_as<expected<void, status_code>>;
    { scope.resolve(index) } -> std::same_as<expected<shared_ptr_decode_entry, status_code>>;
    { scope.mark_complete(index) } -> std::same_as<void>;
    { scope.reset() } -> std::same_as<void>;
};

class shared_ptr_encode_scope {
  private:
    struct key_hash {
        [[nodiscard]] std::size_t operator()(const shared_ptr_encode_key &key) const noexcept {
            const auto target_hash = std::hash<const void *>{}(key.target);
            const auto type_hash   = std::hash<const void *>{}(key.pointer_type);
            return target_hash ^ (type_hash + 0x9e3779b9U + (target_hash << 6U) + (target_hash >> 2U));
        }
    };

    struct entry {
        shared_ptr_encode_key  key{};
        shared_ptr_entry_state state{shared_ptr_entry_state::encoding};
    };

  public:
    [[nodiscard]] expected<shared_ptr_observation, status_code> observe(const shared_ptr_encode_key &key) {
        if (const auto found = lookup_.find(key); found != lookup_.end()) {
            const auto &existing = entries_[found->second];
            if (existing.state != shared_ptr_entry_state::complete) {
                return unexpected<status_code>{status_code::error};
            }
            return shared_ptr_observation{.kind = shared_ptr_observation_kind::reference, .index = found->second};
        }

        const auto index = entries_.size();
        entries_.push_back(entry{.key = key, .state = shared_ptr_entry_state::encoding});
        try {
            lookup_.emplace(key, index);
        } catch (...) {
            entries_.pop_back();
            throw;
        }
        return shared_ptr_observation{.kind = shared_ptr_observation_kind::first, .index = index};
    }

    [[nodiscard]] expected<void, status_code> observe_untracked() {
        entries_.push_back({});
        return {};
    }

    void mark_complete(std::size_t index) {
        if (index < entries_.size()) {
            entries_[index].state = shared_ptr_entry_state::complete;
        }
    }

    void reset() {
        entries_.clear();
        lookup_.clear();
    }

    void reserve(std::size_t count) {
        entries_.reserve(count);
        lookup_.reserve(count);
    }

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

  private:
    std::vector<entry>                                               entries_{};
    std::unordered_map<shared_ptr_encode_key, std::size_t, key_hash> lookup_{};
};

class shared_ptr_decode_scope {
  public:
    [[nodiscard]] expected<std::size_t, status_code> insert(const shared_ptr_decode_entry &entry) {
        const auto index = entries_.size();
        entries_.push_back(entry);
        return index;
    }

    [[nodiscard]] expected<void, status_code> insert_untracked() {
        entries_.push_back({});
        return {};
    }

    [[nodiscard]] expected<shared_ptr_decode_entry, status_code> resolve(std::size_t index) {
        if (index >= entries_.size()) {
            return unexpected<status_code>{status_code::error};
        }
        return entries_[index];
    }

    void mark_complete(std::size_t index) {
        if (index < entries_.size()) {
            entries_[index].state = shared_ptr_entry_state::complete;
        }
    }

    void reset() { entries_.clear(); }
    void reserve(std::size_t count) { entries_.reserve(count); }

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

  private:
    std::vector<shared_ptr_decode_entry> entries_{};
};

static_assert(SharedPtrEncodeScope<shared_ptr_encode_scope>);
static_assert(SharedPtrDecodeScope<shared_ptr_decode_scope>);

template <IsSharedPointer Pointer> class scoped_shared_ptr {
  public:
    using pointer_type = std::remove_cvref_t<Pointer>;
    using element_type = typename pointer_type::element_type;

    scoped_shared_ptr()
        requires std::default_initializable<pointer_type>
    = default;

    explicit scoped_shared_ptr(pointer_type pointer) : pointer_(std::move(pointer)) {}

    [[nodiscard]] element_type *get() const noexcept(noexcept(pointer_.get())) { return pointer_.get(); }
    [[nodiscard]] element_type &operator*() const noexcept(noexcept(*pointer_)) { return *pointer_; }
    explicit operator bool() const noexcept(noexcept(static_cast<bool>(pointer_))) { return static_cast<bool>(pointer_); }

    void reset() noexcept(noexcept(pointer_.reset())) { pointer_.reset(); }
    void reset(element_type *raw) noexcept(noexcept(pointer_.reset(raw))) { pointer_.reset(raw); }

    [[nodiscard]] pointer_type       &value()       &noexcept { return pointer_; }
    [[nodiscard]] const pointer_type &value() const & noexcept { return pointer_; }
    [[nodiscard]] pointer_type      &&value()      &&noexcept { return std::move(pointer_); }

  private:
    pointer_type pointer_;
};

template <IsSharedPointer Pointer>
[[nodiscard]] auto as_scoped_shared_ptr(Pointer pointer) -> scoped_shared_ptr<std::remove_cvref_t<Pointer>> {
    return scoped_shared_ptr<std::remove_cvref_t<Pointer>>{std::move(pointer)};
}

} // namespace cbor::tags::smart_ptr
