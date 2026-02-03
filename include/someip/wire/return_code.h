#pragma once

#include <cstdint>

namespace someip::wire::return_code {

static constexpr std::uint8_t E_OK                     = 0x00;
static constexpr std::uint8_t E_NOT_OK                 = 0x01;
static constexpr std::uint8_t E_UNKNOWN_SERVICE        = 0x02;
static constexpr std::uint8_t E_UNKNOWN_METHOD         = 0x03;
static constexpr std::uint8_t E_NOT_READY              = 0x04;
static constexpr std::uint8_t E_NOT_REACHABLE          = 0x05;
static constexpr std::uint8_t E_TIMEOUT                = 0x06;
static constexpr std::uint8_t E_WRONG_PROTOCOL_VERSION = 0x07;
static constexpr std::uint8_t E_WRONG_INTERFACE_VERSION = 0x08;
static constexpr std::uint8_t E_MALFORMED_MESSAGE      = 0x09;
static constexpr std::uint8_t E_WRONG_MESSAGE_TYPE     = 0x0A;

} // namespace someip::wire::return_code
