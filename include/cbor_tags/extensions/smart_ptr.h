#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_extensions.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace cbor::tags::ext::smart_ptr {

class shared_graph_encode_session;
class shared_graph_decode_session;

template <typename T> struct shared_graph_encode_root {
    shared_graph_encode_session &session;
    const T                     &value;
};

template <typename T> struct shared_graph_decode_root {
    shared_graph_decode_session &session;
    T                           &value;
};

template <typename T> shared_graph_encode_root<T> as_shared_graph(shared_graph_encode_session &session, const T &value) {
    return {session, value};
}

template <typename T> shared_graph_encode_root<T> as_shared_graph(shared_graph_encode_session &, const T &&) = delete;

template <typename T> shared_graph_decode_root<T> as_shared_graph(shared_graph_decode_session &session, T &value) {
    return {session, value};
}

namespace detail {

template <typename T>
concept NullablePointerValue = !std::is_void_v<T> && !std::is_array_v<T> && !std::is_const_v<T>;

struct nullable_ptr_codec_marker {};
struct shared_graph_codec_marker {};

template <typename T> constexpr bool has_nullable_ptr_codec_v = std::is_base_of_v<nullable_ptr_codec_marker, std::remove_cvref_t<T>>;

template <typename T> constexpr bool has_shared_graph_codec_v = std::is_base_of_v<shared_graph_codec_marker, std::remove_cvref_t<T>>;

template <typename Decoder>
[[nodiscard]] status_code decode_definite_array_size(Decoder &dec, major_type major, std::byte additional_info, std::uint64_t &size) {
    if (major != major_type::Array) {
        return status_code::no_match_for_array_on_buffer;
    }
    if (additional_info == static_cast<std::byte>(31)) {
        return status_code::unexpected_group_size;
    }
    size = dec.decode_unsigned(additional_info);
    return status_code::success;
}

template <typename T> inline constexpr char graph_type_token{};

template <typename T> constexpr const void *graph_type_id() noexcept { return &graph_type_token<std::remove_cvref_t<T>>; }

template <typename Encoder, typename Pointer> void encode_nullable_pointer(Encoder &enc, const Pointer &value) {
    if (!value) {
        enc.encode(as_array{1});
        enc.encode(std::uint64_t{0});
        return;
    }

    enc.encode(as_array{2});
    enc.encode(std::uint64_t{1});
    enc.encode(*value);
}

template <typename Decoder, NullablePointerValue T>
[[nodiscard]] status_code decode_nullable_pointer(Decoder &dec, std::unique_ptr<T> &value, major_type major, std::byte additional_info) {
    std::uint64_t size{};
    auto          status = decode_definite_array_size(dec, major, additional_info, size);
    if (status != status_code::success) {
        return status;
    }
    if (size != 1U && size != 2U) {
        return status_code::unexpected_group_size;
    }

    std::uint64_t kind{};
    status = dec.decode(kind);
    if (status != status_code::success) {
        return status;
    }

    if (kind == 0U) {
        if (size != 1U) {
            return status_code::unexpected_group_size;
        }
        value.reset();
        return status_code::success;
    }
    if (kind != 1U) {
        return status_code::error;
    }
    if (size != 2U) {
        return status_code::unexpected_group_size;
    }

    auto decoded = std::make_unique<T>();
    status       = dec.decode(*decoded);
    if (status == status_code::success) {
        value = std::move(decoded);
    }
    return status;
}

template <typename Decoder, NullablePointerValue T>
[[nodiscard]] status_code decode_nullable_pointer(Decoder &dec, std::shared_ptr<T> &value, major_type major, std::byte additional_info) {
    std::uint64_t size{};
    auto          status = decode_definite_array_size(dec, major, additional_info, size);
    if (status != status_code::success) {
        return status;
    }
    if (size != 1U && size != 2U) {
        return status_code::unexpected_group_size;
    }

    std::uint64_t kind{};
    status = dec.decode(kind);
    if (status != status_code::success) {
        return status;
    }

    if (kind == 0U) {
        if (size != 1U) {
            return status_code::unexpected_group_size;
        }
        value.reset();
        return status_code::success;
    }
    if (kind != 1U) {
        return status_code::error;
    }
    if (size != 2U) {
        return status_code::unexpected_group_size;
    }

    auto decoded = std::make_shared<T>();
    status       = dec.decode(*decoded);
    if (status == status_code::success) {
        value = std::move(decoded);
    }
    return status;
}

enum class graph_entry_state { encoding, complete };

template <typename Session> class scoped_graph_session {
  public:
    scoped_graph_session(Session *&active, Session &next) : active_(active), previous_(active) {
        if (previous_ != nullptr && previous_ != &next) {
            throw std::runtime_error("nested shared graph sessions must use the same session object");
        }
        active_ = &next;
    }

    scoped_graph_session(const scoped_graph_session &)            = delete;
    scoped_graph_session &operator=(const scoped_graph_session &) = delete;

    ~scoped_graph_session() { active_ = previous_; }

  private:
    Session *&active_;
    Session  *previous_;
};

} // namespace detail

class shared_graph_encode_session {
  public:
    void reset() {
        encoded_shared_ids_.clear();
        next_shared_id_ = 1;
    }

  private:
    struct encoded_shared_object {
        std::uint64_t               id{};
        const void                 *type{};
        detail::graph_entry_state   state{detail::graph_entry_state::encoding};
        std::shared_ptr<const void> keepalive{};
    };

    std::unordered_map<const void *, encoded_shared_object> encoded_shared_ids_{};
    std::uint64_t                                           next_shared_id_{1};

    template <typename Self> friend struct shared_graph_codec;
};

class shared_graph_decode_session {
  public:
    void reset() { decoded_shared_objects_.clear(); }

  private:
    struct decoded_shared_object {
        std::shared_ptr<void>     value{};
        const void               *type{};
        detail::graph_entry_state state{detail::graph_entry_state::encoding};
    };

    std::unordered_map<std::uint64_t, decoded_shared_object> decoded_shared_objects_{};

    template <typename Self> friend struct shared_graph_codec;
};

template <typename Self> struct nullable_ptr_codec : detail::nullable_ptr_codec_marker, cbor_codec_mixin_base<Self> {
    using cbor_codec_mixin_base<Self>::decode;
    using cbor_codec_mixin_base<Self>::encode;

    template <detail::NullablePointerValue T>
        requires(!detail::has_shared_graph_codec_v<Self>)
    void encode(const std::unique_ptr<T> &value) {
        detail::encode_nullable_pointer(static_cast<Self &>(*this), value);
    }

    template <detail::NullablePointerValue T>
        requires(!detail::has_shared_graph_codec_v<Self>)
    void encode(const std::shared_ptr<T> &value) {
        detail::encode_nullable_pointer(static_cast<Self &>(*this), value);
    }

    template <detail::NullablePointerValue T>
        requires(std::default_initializable<T> && !detail::has_shared_graph_codec_v<Self>)
    [[nodiscard]] status_code decode(std::unique_ptr<T> &value, major_type major, std::byte additional_info) {
        return detail::decode_nullable_pointer(static_cast<Self &>(*this), value, major, additional_info);
    }

    template <detail::NullablePointerValue T>
        requires(std::default_initializable<T> && !detail::has_shared_graph_codec_v<Self>)
    [[nodiscard]] status_code decode(std::shared_ptr<T> &value, major_type major, std::byte additional_info) {
        return detail::decode_nullable_pointer(static_cast<Self &>(*this), value, major, additional_info);
    }
};

template <typename Self> struct shared_graph_codec : detail::shared_graph_codec_marker, cbor_codec_mixin_base<Self> {
    using cbor_codec_mixin_base<Self>::decode;
    using cbor_codec_mixin_base<Self>::encode;

    template <typename T> void encode(shared_graph_encode_root<T> root) {
        detail::scoped_graph_session scope{active_encode_session_, root.session};
        static_cast<Self &>(*this).encode(root.value);
    }

    template <typename T> [[nodiscard]] status_code decode(shared_graph_decode_root<T> root) {
        detail::scoped_graph_session scope{active_decode_session_, root.session};
        return static_cast<Self &>(*this).decode(root.value);
    }

    template <detail::NullablePointerValue T> void encode(const std::unique_ptr<T> &value) {
        detail::encode_nullable_pointer(static_cast<Self &>(*this), value);
    }

    template <detail::NullablePointerValue T> void encode(const std::shared_ptr<T> &value) {
        if (active_encode_session_ == nullptr) {
            if constexpr (detail::has_nullable_ptr_codec_v<Self>) {
                detail::encode_nullable_pointer(static_cast<Self &>(*this), value);
                return;
            } else {
                throw std::runtime_error("shared_ptr graph encoding requires as_shared_graph(...)");
            }
        }

        auto &enc = static_cast<Self &>(*this);
        if (!value) {
            enc.encode(as_array{1});
            enc.encode(std::uint64_t{2});
            return;
        }

        auto       &session = *active_encode_session_;
        const auto *address = static_cast<const void *>(value.get());
        if (const auto existing = session.encoded_shared_ids_.find(address); existing != session.encoded_shared_ids_.end()) {
            if (existing->second.type != detail::graph_type_id<T>()) {
                throw std::runtime_error("shared_ptr graph references must use one static pointer type per object");
            }
            if (existing->second.state == detail::graph_entry_state::encoding) {
                throw std::runtime_error("shared_ptr graph cycles are unsupported");
            }

            enc.encode(as_array{2});
            enc.encode(std::uint64_t{1});
            enc.encode(existing->second.id);
            return;
        }

        const auto id          = session.next_shared_id_++;
        auto       keepalive   = std::shared_ptr<const void>{value, static_cast<const void *>(value.get())};
        auto [entry, inserted] = session.encoded_shared_ids_.emplace(
            address, shared_graph_encode_session::encoded_shared_object{id, detail::graph_type_id<T>(), detail::graph_entry_state::encoding,
                                                                        std::move(keepalive)});
        static_cast<void>(inserted);

        try {
            enc.encode(as_array{3});
            enc.encode(std::uint64_t{0});
            enc.encode(id);
            enc.encode(*value);
            entry->second.state = detail::graph_entry_state::complete;
        } catch (...) {
            session.reset();
            throw;
        }
    }

    template <detail::NullablePointerValue T>
        requires std::default_initializable<T>
    [[nodiscard]] status_code decode(std::unique_ptr<T> &value, major_type major, std::byte additional_info) {
        return detail::decode_nullable_pointer(static_cast<Self &>(*this), value, major, additional_info);
    }

    template <detail::NullablePointerValue T>
        requires std::default_initializable<T>
    [[nodiscard]] status_code decode(std::shared_ptr<T> &value, major_type major, std::byte additional_info) {
        if (active_decode_session_ == nullptr) {
            if constexpr (detail::has_nullable_ptr_codec_v<Self>) {
                return detail::decode_nullable_pointer(static_cast<Self &>(*this), value, major, additional_info);
            } else {
                return status_code::error;
            }
        }

        auto &dec = static_cast<Self &>(*this);

        std::uint64_t size{};
        auto          status = detail::decode_definite_array_size(dec, major, additional_info, size);
        if (status != status_code::success) {
            return status;
        }
        if (size != 1U && size != 2U && size != 3U) {
            return status_code::unexpected_group_size;
        }

        std::uint64_t kind{};
        status = dec.decode(kind);
        if (status != status_code::success) {
            return status;
        }

        if (kind == 0U) {
            return decode_definition(value, size);
        }
        if (kind == 1U) {
            return decode_reference(value, size);
        }
        if (kind == 2U) {
            return decode_null(value, size);
        }
        return status_code::error;
    }

  private:
    template <detail::NullablePointerValue T> [[nodiscard]] status_code decode_null(std::shared_ptr<T> &value, std::uint64_t size) {
        if (size != 1U) {
            return status_code::unexpected_group_size;
        }
        value.reset();
        return status_code::success;
    }

    template <detail::NullablePointerValue T>
        requires std::default_initializable<T>
    [[nodiscard]] status_code decode_definition(std::shared_ptr<T> &value, std::uint64_t size) {
        if (size != 3U) {
            return status_code::unexpected_group_size;
        }

        auto &dec = static_cast<Self &>(*this);

        std::uint64_t id{};
        auto          status = dec.decode(id);
        if (status != status_code::success) {
            return status;
        }
        if (id == 0U || active_decode_session_->decoded_shared_objects_.contains(id)) {
            return status_code::error;
        }

        auto [entry, inserted] = active_decode_session_->decoded_shared_objects_.emplace(
            id, shared_graph_decode_session::decoded_shared_object{{}, detail::graph_type_id<T>(), detail::graph_entry_state::encoding});
        static_cast<void>(inserted);

        try {
            auto decoded = std::make_shared<T>();
            status       = dec.decode(*decoded);
            if (status != status_code::success) {
                active_decode_session_->decoded_shared_objects_.erase(id);
                return status;
            }

            entry->second.value = std::shared_ptr<void>{decoded};
            entry->second.state = detail::graph_entry_state::complete;
            value               = std::move(decoded);
            return status_code::success;
        } catch (...) {
            active_decode_session_->decoded_shared_objects_.erase(id);
            throw;
        }
    }

    template <detail::NullablePointerValue T>
        requires std::default_initializable<T>
    [[nodiscard]] status_code decode_reference(std::shared_ptr<T> &value, std::uint64_t size) {
        if (size != 2U) {
            return status_code::unexpected_group_size;
        }

        auto &dec = static_cast<Self &>(*this);

        std::uint64_t id{};
        auto          status = dec.decode(id);
        if (status != status_code::success) {
            return status;
        }

        const auto existing = active_decode_session_->decoded_shared_objects_.find(id);
        if (existing == active_decode_session_->decoded_shared_objects_.end() ||
            existing->second.state != detail::graph_entry_state::complete || existing->second.type != detail::graph_type_id<T>()) {
            return status_code::error;
        }

        value = std::static_pointer_cast<T>(existing->second.value);
        return status_code::success;
    }

    shared_graph_encode_session *active_encode_session_{};
    shared_graph_decode_session *active_decode_session_{};
};

} // namespace cbor::tags::ext::smart_ptr
