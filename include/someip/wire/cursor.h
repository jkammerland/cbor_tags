#pragma once

#include "someip/status.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

namespace someip::wire {

template <class B>
concept has_push_back_byte = requires(B b, std::byte v) { b.push_back(v); };

template <class B>
concept has_insert_bytes = requires(B b, std::span<const std::byte> s) { b.insert(b.end(), s.begin(), s.end()); };

template <class B>
concept has_resize = requires(B b, std::size_t n) { b.resize(n); };

template <class B>
concept has_data_and_size =
    requires(B b) {
        { b.data() } -> std::convertible_to<std::byte *>;
        { b.size() } -> std::convertible_to<std::size_t>;
    };

template <class B>
concept has_const_data_and_size =
    requires(const B b) {
        { b.data() } -> std::convertible_to<const std::byte *>;
        { b.size() } -> std::convertible_to<std::size_t>;
    };

template <class B>
concept has_index_access = requires(B b, std::size_t i, std::byte v) {
    b[i] = v;
    { b[i] } -> std::convertible_to<std::byte>;
};

template <class OutBuffer>
class writer {
  public:
    explicit writer(OutBuffer &out) : out_(&out) {
        if constexpr (has_push_back_byte<OutBuffer> || has_resize<OutBuffer>) {
            pos_ = static_cast<std::size_t>(out.size());
        } else {
            pos_ = 0;
        }
    }

    [[nodiscard]] std::size_t position() const noexcept { return pos_; }

    expected<void> write_byte(std::byte b) noexcept {
        if constexpr (has_push_back_byte<OutBuffer>) {
            out_->push_back(b);
            ++pos_;
            return {};
        } else if constexpr (has_index_access<OutBuffer>) {
            if (pos_ >= static_cast<std::size_t>(out_->size())) {
                return unexpected<status_code>(status_code::buffer_too_small);
            }
            (*out_)[pos_++] = b;
            return {};
        } else {
            return unexpected<status_code>(status_code::error);
        }
    }

    expected<void> write_bytes(std::span<const std::byte> bytes) noexcept {
        if (bytes.empty()) {
            return {};
        }
        if constexpr (has_insert_bytes<OutBuffer>) {
            out_->insert(out_->end(), bytes.begin(), bytes.end());
            pos_ += bytes.size();
            return {};
        } else if constexpr (has_data_and_size<OutBuffer> && has_resize<OutBuffer>) {
            const auto old_size = static_cast<std::size_t>(out_->size());
            out_->resize(old_size + bytes.size());
            std::memcpy(out_->data() + old_size, bytes.data(), bytes.size());
            pos_ += bytes.size();
            return {};
        } else {
            for (const auto b : bytes) {
                auto st = write_byte(b);
                if (!st) {
                    return st;
                }
            }
            return {};
        }
    }

  private:
    OutBuffer   *out_{};
    std::size_t  pos_{0};
};

class reader {
  public:
    explicit reader(std::span<const std::byte> in) : in_(in) {}

    [[nodiscard]] std::size_t position() const noexcept { return pos_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return in_.size() - pos_; }

    [[nodiscard]] bool empty() const noexcept { return pos_ >= in_.size(); }
    [[nodiscard]] bool empty(std::size_t offset) const noexcept { return (pos_ + offset) >= in_.size(); }

    expected<std::byte> peek_byte(std::size_t offset = 0) const noexcept {
        if (empty(offset)) {
            return unexpected<status_code>(status_code::buffer_overrun);
        }
        return in_[pos_ + offset];
    }

    expected<std::byte> read_byte() noexcept {
        if (empty()) {
            return unexpected<status_code>(status_code::buffer_overrun);
        }
        return in_[pos_++];
    }

    expected<std::span<const std::byte>> read_bytes(std::size_t n) noexcept {
        if (n == 0) {
            return std::span<const std::byte>{};
        }
        if (remaining() < n) {
            return unexpected<status_code>(status_code::buffer_overrun);
        }
        auto view = in_.subspan(pos_, n);
        pos_ += n;
        return view;
    }

    expected<void> skip(std::size_t n) noexcept {
        if (remaining() < n) {
            return unexpected<status_code>(status_code::buffer_overrun);
        }
        pos_ += n;
        return {};
    }

    [[nodiscard]] std::span<const std::byte> remaining_view() const noexcept { return in_.subspan(pos_); }

  private:
    std::span<const std::byte> in_;
    std::size_t                pos_{0};
};

} // namespace someip::wire
