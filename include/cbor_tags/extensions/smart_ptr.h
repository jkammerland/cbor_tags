#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts_checking.h"
#include "cbor_tags/cbor_extensions.h"
#include "cbor_tags/detail/cbor_variant_dispatch.h"
#include "cbor_tags/detail/smart_ptr_traits.h"
#include "cbor_tags/extensions/cddl_traits.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace cbor::tags::ext::smart_ptr {

class shared_graph_encode_session;
class shared_graph_decode_session;

enum class shared_graph_encode_lookup { unordered_map, linear_scan };

template <typename T> struct shared_graph_encode_root;
template <typename T> struct shared_graph_decode_root;

template <typename T> struct shared_graph_cddl {
    using value_type = std::remove_cvref_t<T>;
};

template <typename T> shared_graph_encode_root<T> as_shared_graph(shared_graph_encode_session &session, const T &value);
template <typename T> shared_graph_decode_root<T> as_shared_graph(shared_graph_decode_session &session, T &value);

template <typename T> struct shared_graph_encode_root {
    shared_graph_encode_session &session;
    const T                     &value;

  private:
    shared_graph_encode_root(shared_graph_encode_session &session_, const T &value_) : session(session_), value(value_) {}

    friend shared_graph_encode_root<T> as_shared_graph<T>(shared_graph_encode_session &, const T &);
};

template <typename T> struct shared_graph_decode_root {
    shared_graph_decode_session &session;
    T                           &value;

  private:
    shared_graph_decode_root(shared_graph_decode_session &session_, T &value_) : session(session_), value(value_) {}

    friend shared_graph_decode_root<T> as_shared_graph<T>(shared_graph_decode_session &, T &);
};

template <typename T> shared_graph_encode_root<T> as_shared_graph(shared_graph_encode_session &session, const T &value) {
    return shared_graph_encode_root<T>{session, value};
}

template <typename T> shared_graph_encode_root<T> as_shared_graph(shared_graph_encode_session &, const T &&) = delete;

template <typename T> shared_graph_decode_root<T> as_shared_graph(shared_graph_decode_session &session, T &value) {
    return shared_graph_decode_root<T>{session, value};
}

namespace detail {

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

template <bool GraphTagsPossible, typename Self, typename... Ts>
[[nodiscard]] constexpr status_code decode_variant_with_nullable_pointers_impl(Self &dec, std::variant<Ts...> &value, major_type major,
                                                                               std::byte                     additional_info,
                                                                               std::optional<std::uint64_t> &tag) {
    static_assert(
        ((IsCborMajor<Ts> || decodable_nullable_pointer_v<Ts> || (GraphTagsPossible && decodable_shared_graph_vector_v<Ts>)) && ...),
        "Variant alternatives must be core CBOR types, decodable nullable smart pointers, or shared graph vectors for this "
        "codec.");
    static_assert(decodable_nullable_pointer_count_v<Ts...> <= 1U,
                  "Variant nullable smart pointer alternatives are ambiguous because they share the same [0] / [1, value] shape.");

    using variant_type          = std::variant<Ts...>;
    using major_index           = cbor::tags::detail::MajorIndex;
    constexpr auto core_mapping = valid_concept_mapping_array_v<variant_type>;
    static_assert(decodable_nullable_pointer_count_v<Ts...> == 0U || core_mapping[major_index::Array] == 0,
                  "Variant nullable smart pointer alternatives are ambiguous with other array-shaped alternatives.");
    static_assert(decodable_shared_graph_vector_count_v<Ts...> == 0U || core_mapping[major_index::Array] == 1U,
                  "Variant shared graph vector alternatives are ambiguous with other array-shaped alternatives.");
    static_assert(!GraphTagsPossible || !variant_has_shared_graph_tag_collision_v<Ts...>,
                  "Variant shared_ptr alternatives in a shared graph collide with tags 28/29 or a catch-all tag alternative.");
    static_assert(core_mapping[major_index::Unsigned] <= 1, "Multiple types match against major type 0 (unsigned integer)");
    static_assert(core_mapping[major_index::Negative] <= 1, "Multiple types match against major type 1 (negative integer)");
    static_assert(core_mapping[major_index::BStr] <= 1, "Multiple types match against major type 2 (byte string)");
    static_assert(core_mapping[major_index::TStr] <= 1, "Multiple types match against major type 3 (text string)");
    static_assert(core_mapping[major_index::Map] <= 1, "Multiple types match against major type 5 (map)");
    static_assert(core_mapping[major_index::Tag] <= 1, "Multiple types match against major type 6 (tag)");
    static_assert(core_mapping[major_index::DynamicTag] == 0,
                  "Variant cannot contain dynamic tags, must be known at compile time, use as_tag_any to catch any tag");
    static_assert(valid_concept_mapping_v<variant_type>,
                  "Variant has ambiguous major types; only one alternative may match each core CBOR dispatch shape.");

    bool                       saw_incomplete = false;
    std::optional<status_code> pointer_error;

    auto try_decode = [&dec, major, additional_info, &value, &tag, &saw_incomplete,
                       &pointer_error]<bool CatchAllPass, typename U>() -> bool {
        using raw_type = std::remove_cvref_t<U>;

        if (pointer_error.has_value()) {
            return false;
        }

        auto read_tag_once = [&dec, additional_info, &tag] {
            if (!tag.has_value()) {
                tag = dec.decode_unsigned(additional_info);
            }
            return *tag;
        };

        if constexpr (decodable_nullable_pointer_v<raw_type>) {
            const auto pointer_major =
                major == major_type::Array || (GraphTagsPossible && decodable_shared_pointer_v<raw_type> && major == major_type::Tag);
            if (!pointer_major) {
                return false;
            }

            raw_type    decoded_value{};
            status_code result;
            if constexpr (GraphTagsPossible) {
                if constexpr (decodable_shared_pointer_v<raw_type>) {
                    if (major == major_type::Tag) {
                        const auto tag_value = read_tag_once();
                        if (tag_value != shareable_tag && tag_value != sharedref_tag) {
                            return false;
                        }
                        result = dec.decode_shared_graph_pointer(decoded_value, tag_value);
                    } else {
                        result = dec.decode(decoded_value, major, additional_info);
                    }
                } else {
                    result = dec.decode(decoded_value, major, additional_info);
                }
            } else {
                result = dec.decode(decoded_value, major, additional_info);
            }
            if (result == status_code::success) {
                value = std::move(decoded_value);
                return true;
            }
            if (result == status_code::incomplete) {
                saw_incomplete = true;
            } else {
                pointer_error = result;
            }
            return false;
        } else {
            if (!cbor::tags::detail::matches_major_dispatch<raw_type>(major)) {
                return false;
            }
            if (major == major_type::Simple && !cbor::tags::detail::matches_simple_dispatch<CatchAllPass, raw_type>(additional_info)) {
                return false;
            }

            raw_type    decoded_value{};
            status_code result;
            if constexpr (IsVariant<raw_type>) {
                result = decode_variant_with_nullable_pointers_impl<GraphTagsPossible>(dec, decoded_value, major, additional_info, tag);
            } else if constexpr (IsTag<raw_type>) {
                result = dec.decode(decoded_value, read_tag_once());
            } else if constexpr (GraphTagsPossible && decodable_shared_graph_vector_v<raw_type>) {
                result = dec.decode_shared_graph_vector_variant_alternative(decoded_value, major, additional_info);
            } else {
                result = dec.decode(decoded_value, major, additional_info);
            }

            if (result == status_code::success) {
                value = std::move(decoded_value);
                return true;
            }
            if (result == status_code::incomplete) {
                saw_incomplete = true;
            } else if constexpr (GraphTagsPossible && decodable_shared_graph_vector_v<raw_type>) {
                pointer_error = result;
            }
            return false;
        }
    };

    bool found = false;
    if (major == major_type::Simple) {
        found = (try_decode.template operator()<false, Ts>() || ...);
        if (!found) {
            found = (try_decode.template operator()<true, Ts>() || ...);
        }
    } else {
        found = (try_decode.template operator()<false, Ts>() || ...);
    }
    if (!found) {
        if (pointer_error.has_value()) {
            return *pointer_error;
        }
        if (saw_incomplete) {
            return status_code::incomplete;
        }
        return status_code::no_match_in_variant_on_buffer;
    }
    return status_code::success;
}

template <bool GraphTagsPossible, typename Self, typename... Ts>
[[nodiscard]] constexpr status_code decode_variant_with_nullable_pointers(Self &dec, std::variant<Ts...> &value, major_type major,
                                                                          std::byte additional_info) {
    std::optional<std::uint64_t> tag;
    return decode_variant_with_nullable_pointers_impl<GraphTagsPossible>(dec, value, major, additional_info, tag);
}

enum class graph_entry_state { encoding, complete };

struct encoded_shared_object {
    const void                 *address{};
    std::uint64_t               id{};
    const void                 *type{};
    graph_entry_state           state{graph_entry_state::encoding};
    std::shared_ptr<const void> keepalive{};
};

template <typename Session> class scoped_graph_session {
  public:
    scoped_graph_session(Session *&active, Session &next) : active_(active), previous_(active), session_(&next) {
        if (previous_ != nullptr && previous_ != &next) {
            throw std::runtime_error("nested shared graph sessions must use the same session object");
        }
        session_->begin_use();
        active_ = &next;
    }

    scoped_graph_session(const scoped_graph_session &)            = delete;
    scoped_graph_session &operator=(const scoped_graph_session &) = delete;

    ~scoped_graph_session() {
        active_ = previous_;
        session_->end_use();
    }

  private:
    Session *&active_;
    Session  *previous_;
    Session  *session_;
};

} // namespace detail

class shared_graph_encode_session {
  public:
    explicit shared_graph_encode_session(shared_graph_encode_lookup lookup = shared_graph_encode_lookup::unordered_map) noexcept
        : lookup_(lookup) {}

    shared_graph_encode_session(const shared_graph_encode_session &)            = delete;
    shared_graph_encode_session &operator=(const shared_graph_encode_session &) = delete;
    shared_graph_encode_session(shared_graph_encode_session &&)                 = delete;
    shared_graph_encode_session &operator=(shared_graph_encode_session &&)      = delete;

    [[nodiscard]] shared_graph_encode_lookup lookup() const noexcept { return lookup_; }

    void reserve_unique(std::size_t unique_count) {
        if (active_depth_ != 0U) {
            throw std::runtime_error("shared graph sessions cannot reserve while an encode operation is active");
        }
        encoded_shared_objects_.reserve(unique_count);
        if (lookup_ == shared_graph_encode_lookup::unordered_map) {
            encoded_shared_ids_.reserve(unique_count);
        }
    }

    void reset() {
        if (active_depth_ != 0U) {
            throw std::runtime_error("shared graph sessions cannot reset while an encode operation is active");
        }
        reset_unchecked();
    }

  private:
    void reset_unchecked() {
        encoded_shared_ids_.clear();
        encoded_shared_objects_.clear();
    }

    std::vector<detail::encoded_shared_object>    encoded_shared_objects_{};
    std::unordered_map<const void *, std::size_t> encoded_shared_ids_{};
    shared_graph_encode_lookup                    lookup_{shared_graph_encode_lookup::unordered_map};
    std::size_t                                   active_depth_{0};

    void begin_use() { ++active_depth_; }
    void end_use() { --active_depth_; }

    [[nodiscard]] std::optional<std::size_t> find_encoded_index(const void *address) const {
        if (lookup_ == shared_graph_encode_lookup::unordered_map) {
            if (const auto existing = encoded_shared_ids_.find(address); existing != encoded_shared_ids_.end()) {
                return existing->second;
            }
            return std::nullopt;
        }

        for (std::size_t index = 0; index < encoded_shared_objects_.size(); ++index) {
            if (encoded_shared_objects_[index].address == address) {
                return index;
            }
        }
        return std::nullopt;
    }

    void index_encoded_address(const void *address, std::size_t index) {
        if (lookup_ == shared_graph_encode_lookup::unordered_map) {
            encoded_shared_ids_.emplace(address, index);
        }
    }

    template <typename Self> friend struct shared_graph_codec;
    template <typename Session> friend class detail::scoped_graph_session;
};

class shared_graph_decode_session {
  public:
    shared_graph_decode_session() = default;

    shared_graph_decode_session(const shared_graph_decode_session &)            = delete;
    shared_graph_decode_session &operator=(const shared_graph_decode_session &) = delete;
    shared_graph_decode_session(shared_graph_decode_session &&)                 = delete;
    shared_graph_decode_session &operator=(shared_graph_decode_session &&)      = delete;

    void reset() {
        if (active_depth_ != 0U) {
            throw std::runtime_error("shared graph sessions cannot reset while a decode operation is active");
        }
        reset_unchecked();
    }

  private:
    void reset_unchecked() { decoded_shared_objects_.clear(); }

    struct decoded_shared_object {
        std::shared_ptr<void>     value{};
        const void               *type{};
        detail::graph_entry_state state{detail::graph_entry_state::encoding};
    };

    std::vector<decoded_shared_object> decoded_shared_objects_{};
    std::size_t                        active_depth_{0};

    void rollback_to(std::size_t checkpoint) { decoded_shared_objects_.resize(checkpoint); }

    void begin_use() { ++active_depth_; }
    void end_use() { --active_depth_; }

    template <typename Self> friend struct shared_graph_codec;
    template <typename Session> friend class detail::scoped_graph_session;
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

    template <typename... Ts>
        requires(detail::has_decodable_nullable_pointer_v<Ts...> && !detail::has_shared_graph_codec_v<Self>)
    [[nodiscard]] status_code decode(std::variant<Ts...> &value, major_type major, std::byte additional_info) {
        return detail::decode_variant_with_nullable_pointers<false>(static_cast<Self &>(*this), value, major, additional_info);
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
            enc.encode(std::uint64_t{0});
            return;
        }

        auto       &session = *active_encode_session_;
        const auto *address = static_cast<const void *>(value.get());
        if (const auto existing = session.find_encoded_index(address)) {
            auto &entry = session.encoded_shared_objects_[*existing];
            if (entry.type != detail::graph_type_id<T>()) {
                throw std::runtime_error("shared_ptr graph references must use one static pointer type per object");
            }
            if (entry.state == detail::graph_entry_state::encoding) {
                throw std::runtime_error("shared_ptr graph cycles are unsupported");
            }

            enc.encode(static_tag<detail::sharedref_tag>{});
            enc.encode(entry.id);
            return;
        }

        const auto id        = static_cast<std::uint64_t>(session.encoded_shared_objects_.size());
        auto       keepalive = std::shared_ptr<const void>{value, static_cast<const void *>(value.get())};

        session.encoded_shared_objects_.push_back(detail::encoded_shared_object{address, id, detail::graph_type_id<T>(),
                                                                                detail::graph_entry_state::encoding, std::move(keepalive)});
        try {
            session.index_encoded_address(address, session.encoded_shared_objects_.size() - 1U);
        } catch (...) {
            session.encoded_shared_objects_.pop_back();
            throw;
        }

        try {
            enc.encode(static_tag<detail::shareable_tag>{});
            enc.encode(*value);
            session.encoded_shared_objects_[id].state = detail::graph_entry_state::complete;
        } catch (...) {
            session.reset_unchecked();
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

        if (major == major_type::Array) {
            return decode_null(value, major, additional_info);
        }
        if (major != major_type::Tag) {
            return status_code::no_match_for_tag_on_buffer;
        }

        const auto tag = dec.decode_unsigned(additional_info);
        return decode_shared_graph_pointer(value, tag);
    }

    template <typename... Ts>
        requires(detail::has_decodable_nullable_pointer_v<Ts...> || detail::has_decodable_shared_graph_vector_v<Ts...>)
    [[nodiscard]] status_code decode(std::variant<Ts...> &value, major_type major, std::byte additional_info) {
        if (active_decode_session_ == nullptr) {
            if constexpr (detail::has_nullable_ptr_codec_v<Self> && detail::has_decodable_nullable_pointer_v<Ts...> &&
                          !detail::has_decodable_shared_graph_vector_v<Ts...>) {
                return detail::decode_variant_with_nullable_pointers<false>(static_cast<Self &>(*this), value, major, additional_info);
            } else {
                return status_code::error;
            }
        }
        if constexpr (detail::variant_has_shared_graph_tag_collision_v<Ts...>) {
            return status_code::error;
        } else {
            return detail::decode_variant_with_nullable_pointers<true>(static_cast<Self &>(*this), value, major, additional_info);
        }
    }

  private:
    template <bool GraphTagsPossible, typename FriendSelf, typename... Ts>
    friend constexpr status_code detail::decode_variant_with_nullable_pointers_impl(FriendSelf &, std::variant<Ts...> &, major_type,
                                                                                    std::byte, std::optional<std::uint64_t> &);

    template <detail::NullablePointerValue T>
        requires std::default_initializable<T>
    [[nodiscard]] status_code decode_shared_graph_pointer(std::shared_ptr<T> &value, std::uint64_t tag) {
        if (active_decode_session_ == nullptr) {
            return status_code::error;
        }
        if (tag == detail::shareable_tag) {
            return decode_shareable(value);
        }
        if (tag == detail::sharedref_tag) {
            return decode_sharedref(value);
        }
        return status_code::no_match_for_tag;
    }

    template <typename Vector>
    [[nodiscard]] status_code decode_shared_graph_vector_variant_alternative(Vector &value, major_type major, std::byte additional_info) {
        if (active_decode_session_ == nullptr) {
            return status_code::error;
        }

        auto      &session    = *active_decode_session_;
        const auto checkpoint = session.decoded_shared_objects_.size();
        try {
            auto status = static_cast<Self &>(*this).decode(value, major, additional_info);
            if (status != status_code::success) {
                session.rollback_to(checkpoint);
            }
            return status;
        } catch (...) {
            session.rollback_to(checkpoint);
            throw;
        }
    }

    template <detail::NullablePointerValue T>
    [[nodiscard]] status_code decode_null(std::shared_ptr<T> &value, major_type major, std::byte additional_info) {
        auto &dec = static_cast<Self &>(*this);

        std::uint64_t size{};
        auto          status = detail::decode_definite_array_size(dec, major, additional_info, size);
        if (status != status_code::success) {
            return status;
        }
        if (size != 1U) {
            return status_code::unexpected_group_size;
        }

        std::uint64_t kind{};
        status = dec.decode(kind);
        if (status != status_code::success) {
            return status;
        }
        if (kind != 0U) {
            return status_code::error;
        }

        value.reset();
        return status_code::success;
    }

    template <detail::NullablePointerValue T>
        requires std::default_initializable<T>
    [[nodiscard]] status_code decode_shareable(std::shared_ptr<T> &value) {
        auto &dec = static_cast<Self &>(*this);

        auto      &session    = *active_decode_session_;
        const auto checkpoint = session.decoded_shared_objects_.size();
        const auto id         = session.decoded_shared_objects_.size();

        try {
            session.decoded_shared_objects_.push_back(
                shared_graph_decode_session::decoded_shared_object{{}, detail::graph_type_id<T>(), detail::graph_entry_state::encoding});

            auto decoded = std::make_shared<T>();
            auto status  = dec.decode(*decoded);
            if (status != status_code::success) {
                session.rollback_to(checkpoint);
                return status;
            }

            auto &entry = session.decoded_shared_objects_[id];
            entry.value = std::shared_ptr<void>{decoded};
            entry.state = detail::graph_entry_state::complete;
            value       = std::move(decoded);
            return status_code::success;
        } catch (...) {
            session.rollback_to(checkpoint);
            throw;
        }
    }

    template <detail::NullablePointerValue T>
        requires std::default_initializable<T>
    [[nodiscard]] status_code decode_sharedref(std::shared_ptr<T> &value) {
        auto &dec = static_cast<Self &>(*this);

        std::uint64_t id{};
        auto          status = dec.decode(id);
        if (status != status_code::success) {
            return status;
        }

        auto &session = *active_decode_session_;
        if (id >= session.decoded_shared_objects_.size()) {
            return status_code::error;
        }

        const auto &existing = session.decoded_shared_objects_[id];
        if (existing.state != detail::graph_entry_state::complete || existing.type != detail::graph_type_id<T>()) {
            return status_code::error;
        }

        value = std::static_pointer_cast<T>(existing.value);
        return status_code::success;
    }

    shared_graph_encode_session *active_encode_session_{};
    shared_graph_decode_session *active_decode_session_{};
};

} // namespace cbor::tags::ext::smart_ptr

namespace cbor::tags::cddl {

template <typename T> struct cddl_scope_traits<ext::smart_ptr::shared_graph_cddl<T>> {
    using value_type = typename ext::smart_ptr::shared_graph_cddl<T>::value_type;

    static constexpr cddl_shared_pointer_mode shared_pointer_mode = cddl_shared_pointer_mode::shared_graph;
};

} // namespace cbor::tags::cddl
