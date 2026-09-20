#pragma once

#include <cstdint>

namespace gatekeeper::protocol::gkwp2
{

enum class BinaryOpcode : std::uint16_t
{
    Ping = 0x0001,
    Set = 0x0010,
    Get = 0x0011,
    Del = 0x0012,
    Incr = 0x0020,
    RateLimit = 0x0100,
    Reserve = 0x0101,
    Commit = 0x0102,
};

enum class BinaryStatus : std::uint8_t
{
    Ok = 0x00,
    Error = 0x01,
    NotFound = 0x02,
    Denied = 0x03,
};

}
