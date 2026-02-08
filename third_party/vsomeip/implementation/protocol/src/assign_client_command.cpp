// Copyright (C) 2021 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <cstddef>
#include <limits>

#include <vsomeip/internal/logger.hpp>
#include "../include/assign_client_command.hpp"

namespace vsomeip_v3 {
namespace protocol {

assign_client_command::assign_client_command() : command(id_e::ASSIGN_CLIENT_ID) { }

void assign_client_command::serialize(std::vector<byte_t>& _buffer, error_e& _error) const {

    size_t its_size(COMMAND_HEADER_SIZE + name_.length());

    if (its_size > std::numeric_limits<command_size_t>::max()) {

        _error = error_e::ERROR_MAX_COMMAND_SIZE_EXCEEDED;
        return;
    }

    // resize buffer
    _buffer.resize(its_size);

    // set size
    size_ = static_cast<command_size_t>(name_.length());

    // serialize header
    command::serialize(_buffer, _error);
    if (_error != error_e::ERROR_OK)
        return;

    // serialize payload
    if (!name_.empty())
        std::memcpy(&_buffer[COMMAND_POSITION_PAYLOAD], name_.data(), name_.length());
}

void assign_client_command::deserialize(const std::vector<byte_t>& _buffer, error_e& _error) {

    if (_buffer.size() < COMMAND_HEADER_SIZE) {

        _error = error_e::ERROR_NOT_ENOUGH_BYTES;
        return;
    }

    command::deserialize(_buffer, _error);
    if (_error != error_e::ERROR_OK)
        return;

    const auto payload_size = static_cast<std::size_t>(size_);
    const auto payload_end = COMMAND_POSITION_PAYLOAD + payload_size;
    if (payload_end < COMMAND_POSITION_PAYLOAD || payload_end > _buffer.size()) {
        _error = error_e::ERROR_NOT_ENOUGH_BYTES;
        return;
    }

    if (payload_size == 0U) {
        name_.clear();
        return;
    }

    name_.assign(_buffer.begin() + static_cast<std::ptrdiff_t>(COMMAND_POSITION_PAYLOAD),
                 _buffer.begin() + static_cast<std::ptrdiff_t>(payload_end));
}

std::string assign_client_command::get_name() const {

    return name_;
}

void assign_client_command::set_name(const std::string& _name) {

    name_ = _name;
}

} // namespace protocol
} // namespace vsomeip
