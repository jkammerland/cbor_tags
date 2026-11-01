#pragma once

#include "resumable_ring_fixture.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <stdexcept>

namespace resumable_test {

// Fixed packet slots reuse the ring fixture's public absolute-position protocol.
// A transport loan keeps its whole slot unavailable until send completion.
template <bool Output, std::size_t PacketSize, std::size_t PacketCount>
class packet_pool_source : public ring_source<Output, PacketSize * PacketCount> {
    using base_type = ring_source<Output, PacketSize * PacketCount>;

  public:
    using position_type = typename base_type::position_type;

    struct packet {
        position_type first;
        position_type last;
    };

    explicit packet_pool_source(std::size_t initial_offset = 100)
        : base_type(initial_offset), initial_(initial_offset), published_(initial_offset) {
        if constexpr (Output) {
            std::array<std::byte, PacketSize * PacketCount> poison;
            poison.fill(std::byte{0xa5});
            this->write(poison);
            this->restore(this->committed_position());
        }
    }

    std::optional<packet> publish()
        requires(Output)
    {
        if (published_ == this->committed_position().offset())
            return std::nullopt;
        const auto index = slot(published_);
        if (loans_[index])
            return std::nullopt;

        const auto boundary = initial_ + ((published_ - initial_) / PacketSize + 1) * PacketSize;
        const auto limit    = std::min(boundary, this->committed_position().offset());
        packet     result{{this, published_}, {this, limit}};
        loans_[index] = result;
        published_    = limit;
        return result;
    }

    void complete_send(const packet &sent)
        requires(Output)
    {
        auto &loan = loans_[slot(sent.first.offset())];
        if (!loan || loan->first != sent.first || loan->last != sent.last)
            throw std::logic_error("packet does not own this transport loan");
        loan.reset();
    }

    void reclaim_ready() {
        auto through = this->committed_position().offset();
        if constexpr (Output) {
            through = std::min(through, published_);
            for (const auto &loan : loans_) {
                if (loan)
                    through = std::min(through, loan->first.offset());
            }
        }

        // Retire only complete slots; uncommitted or borrowed bytes keep a slot.
        through = initial_ + (through - initial_) / PacketSize * PacketSize;
        retired_packets_ += (through - this->begin().offset()) / PacketSize;
        base_type::reclaim(through);
    }

    std::size_t retired_packets() const { return retired_packets_; }

  private:
    std::size_t slot(std::size_t offset) const { return (offset - initial_) / PacketSize % PacketCount; }

    std::size_t                                    initial_;
    std::size_t                                    published_;
    std::size_t                                    retired_packets_{};
    std::array<std::optional<packet>, PacketCount> loans_;
};

static_assert(cbor::tags::ResumableInputSource<packet_pool_source<false, 11, 3>>);
static_assert(cbor::tags::ResumableOutputSource<packet_pool_source<true, 11, 3>>);

} // namespace resumable_test
